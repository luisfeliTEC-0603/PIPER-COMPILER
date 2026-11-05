"""Autocompletado asíncrono respaldado por la tabla LL(1) de Pipoc."""

from pathlib import Path
import os
import tempfile

from PySide6.QtCore import QProcess, QStringListModel, Qt, QTimer, QObject, Signal
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import QCompleter

from ..language.context import CompletionContext, completion_context
from ..language.tokens import completion_candidates
from ..services.compiler import CompilerConfigurationError, find_compiler


class CompletionService(QObject):
    """Ejecuta ``pipoc --ll1-expected`` sin bloquear el hilo de la interfaz."""

    ready = Signal(int, tuple)
    unavailable = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._request_id = 0
        self._process: QProcess | None = None
        self._temporary: Path | None = None

    def request(self, source_prefix: str, project_root: Path) -> int | None:
        """Escribe el prefijo temporal y solicita terminales a Pipoc."""
        self.cancel()
        try:
            compiler = find_compiler(project_root)
            handle = tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", suffix=".rwt", delete=False,
            )
            with handle:
                handle.write(source_prefix)
        except (CompilerConfigurationError, OSError) as error:
            self.unavailable.emit(str(error))
            return None

        self._request_id += 1
        request_id = self._request_id
        self._temporary = Path(handle.name)
        process = QProcess(self)
        process.setProgram(str(compiler))
        process.setArguments(("--ll1-expected", str(self._temporary)))
        process.setWorkingDirectory(str(project_root))
        process.finished.connect(
            lambda exit_code, status: self._finished(
                request_id, process, exit_code, status,
            )
        )
        process.errorOccurred.connect(
            lambda error: self._process_error(request_id, process, error)
        )
        self._process = process
        process.start()
        return request_id

    def cancel(self):
        """Descarta una consulta anterior y elimina su archivo temporal."""
        if self._process is not None:
            self._process.blockSignals(True)
            if self._process.state() != QProcess.ProcessState.NotRunning:
                self._process.kill()
            self._process.deleteLater()
            self._process = None
        self._remove_temporary()

    def stop(self):
        """Detiene cualquier consulta al cerrar la ventana."""
        self.cancel()

    def _finished(self, request_id: int, process: QProcess, exit_code: int, _status):
        """Publica líneas limpias solo si la respuesta continúa vigente."""
        if process is not self._process or request_id != self._request_id:
            process.deleteLater()
            return
        output = bytes(process.readAllStandardOutput()).decode("utf-8", "replace")
        expected = tuple(line.strip() for line in output.splitlines() if line.strip())
        self._process = None
        process.deleteLater()
        self._remove_temporary()
        self.ready.emit(request_id, expected if exit_code == 0 else ())

    def _process_error(self, request_id: int, process: QProcess, error):
        """Informa solamente fallos de arranque; un prefijo inválido es normal."""
        if (process is self._process and request_id == self._request_id
                and error == QProcess.ProcessError.FailedToStart):
            self._process = None
            process.deleteLater()
            self._remove_temporary()
            self.unavailable.emit(process.errorString())

    def _remove_temporary(self):
        """Elimina exclusivamente el prefijo creado por este servicio."""
        if self._temporary is not None:
            try:
                os.unlink(self._temporary)
            except (FileNotFoundError, OSError):
                pass
            self._temporary = None


class CompletionController(QObject):
    """Coordina debounce, popup e inserción sobre un ``CodeEditor``."""

    unavailable = Signal(str)

    def __init__(self, editor, parent=None):
        super().__init__(parent)
        self.editor = editor
        self.service = CompletionService(self)
        self.model = QStringListModel(self)
        self.completer = QCompleter(self.model, self)
        self.completer.setWidget(editor)
        self.completer.setCaseSensitivity(Qt.CaseSensitivity.CaseSensitive)
        self.completer.setCompletionMode(QCompleter.CompletionMode.PopupCompletion)
        self.completer.activated[str].connect(self._insert)
        self.service.ready.connect(self._show)
        self.service.unavailable.connect(self.unavailable.emit)
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(180)
        self.timer.timeout.connect(self._request_automatic)
        self.enabled = False
        self.project_root = Path.cwd()
        self._pending_id: int | None = None
        self._pending_context: CompletionContext | None = None
        editor.set_completer(self.completer)
        editor.textChanged.connect(self.schedule)
        editor.cursorPositionChanged.connect(self.hide)
        editor.completionRequested.connect(self.request_now)

    def configure(self, enabled: bool, project_root: Path):
        """Activa sugerencias solo para un buffer Piper y fija el Pipoc cercano."""
        resolved_root = Path(project_root).resolve()
        if self.enabled == enabled and self.project_root == resolved_root:
            return
        self.enabled = enabled
        self.project_root = resolved_root
        self.hide()
        self.timer.stop()
        if not enabled:
            self.service.cancel()

    def schedule(self):
        """Agrupa pulsaciones; el popup automático exige una palabra parcial."""
        self.hide()
        if not self.enabled:
            return
        context = self._context()
        if context.suppressed or not context.fragment:
            self.timer.stop()
            return
        self.timer.start()

    def request_now(self):
        """Solicita opciones incluso sin fragmento mediante Ctrl+Espacio."""
        self.timer.stop()
        self._request(explicit=True)

    def _request_automatic(self):
        self._request(explicit=False)

    def _request(self, explicit: bool):
        if not self.enabled:
            return
        context = self._context()
        if context.suppressed or (not explicit and not context.fragment):
            return
        self._pending_context = context
        self._pending_id = self.service.request(
            context.parse_prefix, self.project_root,
        )

    def _show(self, request_id: int, expected: tuple[str, ...]):
        """Descarta respuestas obsoletas y abre el popup junto al cursor."""
        if request_id != self._pending_id or not self.enabled:
            return
        current = self._context()
        pending = self._pending_context
        if pending is None or current.suppressed or current.parse_prefix != pending.parse_prefix:
            return
        candidates = completion_candidates(
            expected, current.fragment, self.editor.toPlainText(),
        )
        if not candidates:
            self.hide()
            return
        self.model.setStringList(list(candidates))
        self.completer.setCompletionPrefix(current.fragment)
        first = self.completer.completionModel().index(0, 0)
        self.completer.popup().setCurrentIndex(first)
        rect = self.editor.cursorRect()
        width = max(
            180,
            min(420, max(self.editor.fontMetrics().horizontalAdvance(item)
                         for item in candidates) + 38),
        )
        rect.setWidth(width)
        self.completer.complete(rect)

    def _insert(self, completion: str):
        """Reemplaza solamente el fragmento vigente y conserva Deshacer de Qt."""
        context = self._context()
        cursor = self.editor.textCursor()
        for _ in context.fragment:
            cursor.movePosition(
                QTextCursor.MoveOperation.PreviousCharacter,
                QTextCursor.MoveMode.KeepAnchor,
            )
        cursor.insertText(completion)
        self.editor.setTextCursor(cursor)

    def _context(self) -> CompletionContext:
        cursor = QTextCursor(self.editor.textCursor())
        cursor.setPosition(0, QTextCursor.MoveMode.KeepAnchor)
        before = cursor.selectedText().replace("\u2029", "\n")
        return completion_context(before)

    def hide(self):
        """Oculta resultados que ya no corresponden a la posición actual."""
        self.completer.popup().hide()

    def stop(self):
        """Cancela debounce y proceso auxiliar al cerrar la aplicación."""
        self.timer.stop()
        self.service.stop()
