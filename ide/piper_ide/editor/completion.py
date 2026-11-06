"""Autocompletado asíncrono respaldado por la tabla LL(1) de Pipoc.

Cada petición queda vinculada al documento, su revisión y la selección exacta.
El parser C puede responder después de otra tecla: esa respuesta no debe abrir
una lista para un cursor o una palabra que ya cambiaron.
"""

from dataclasses import dataclass
from pathlib import Path
import os
import tempfile

from PySide6.QtCore import QEvent, QProcess, QSize, Qt, QTimer, QObject, Signal
from PySide6.QtGui import (
    QColor, QFont, QFontMetrics, QPalette, QStandardItem, QStandardItemModel,
    QTextCursor, QTextDocument,
)
from PySide6.QtWidgets import (
    QAbstractItemView, QApplication, QListView, QStyle, QStyledItemDelegate,
    QStyleOptionViewItem, QWidget,
)

from ..language.context import CompletionContext, completion_context
from ..language.tokens import completion_items
from ..services.compiler import CompilerConfigurationError, find_compiler


DETAIL_ROLE = Qt.ItemDataRole.UserRole + 1


@dataclass(frozen=True)
class _EditorState:
    """Foto exacta de la edición para rechazar resultados de consultas anteriores.

    Dos buffers pueden tener el mismo texto; por eso se conserva el documento.
    La revisión distingue ediciones y deshacer, y la selección impide insertar
    en una posición diferente aunque el prefijo analizable sea idéntico.
    """

    document: QTextDocument
    revision: int
    position: int
    anchor: int
    context: CompletionContext


class CompletionItemDelegate(QStyledItemDelegate):
    """Dibuja cada sugerencia como nombre y guía de uso en dos líneas."""

    _HORIZONTAL_PADDING = 10
    _VERTICAL_PADDING = 5

    def paint(self, painter, option, index):
        """Conserva selección/hover nativos y añade el detalle monoespaciado."""
        visual = QStyleOptionViewItem(option)
        self.initStyleOption(visual, index)
        title = visual.text
        detail = index.data(DETAIL_ROLE) or ""
        visual.text = ""
        style = visual.widget.style() if visual.widget else QApplication.style()
        style.drawControl(
            QStyle.ControlElement.CE_ItemViewItem, visual, painter, visual.widget,
        )

        painter.save()
        painter.setClipRect(option.rect)
        selected = bool(option.state & QStyle.StateFlag.State_Selected)
        title_role = (
            QPalette.ColorRole.HighlightedText
            if selected else QPalette.ColorRole.Text
        )
        detail_role = (
            QPalette.ColorRole.HighlightedText
            if selected else QPalette.ColorRole.PlaceholderText
        )
        title_color = option.palette.color(title_role)
        detail_color = QColor(option.palette.color(detail_role))
        if selected:
            detail_color.setAlpha(205)

        title_font = QFont(option.font)
        title_font.setBold(True)
        title_metrics = QFontMetrics(title_font)
        content = option.rect.adjusted(
            self._HORIZONTAL_PADDING, self._VERTICAL_PADDING,
            -self._HORIZONTAL_PADDING, -self._VERTICAL_PADDING,
        )
        painter.setFont(title_font)
        painter.setPen(title_color)
        painter.drawText(
            content.x(), content.y(), content.width(), title_metrics.height(),
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
            title_metrics.elidedText(title, Qt.TextElideMode.ElideRight, content.width()),
        )

        if detail:
            detail_font = QFont(option.font)
            detail_metrics = QFontMetrics(detail_font)
            painter.setFont(detail_font)
            painter.setPen(detail_color)
            painter.drawText(
                content.x(), content.y() + title_metrics.height() + 1,
                content.width(), detail_metrics.height(),
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                detail_metrics.elidedText(
                    detail, Qt.TextElideMode.ElideRight, content.width(),
                ),
            )
        painter.restore()

    def sizeHint(self, option, index):
        """Reserva una segunda línea únicamente cuando existe documentación."""
        title = str(index.data(Qt.ItemDataRole.DisplayRole) or "")
        detail = str(index.data(DETAIL_ROLE) or "")
        normal = QFontMetrics(option.font)
        title_font = QFont(option.font)
        title_font.setBold(True)
        title_metrics = QFontMetrics(title_font)
        width = max(
            title_metrics.horizontalAdvance(title),
            normal.horizontalAdvance(detail),
        ) + self._HORIZONTAL_PADDING * 2
        lines_height = title_metrics.height()
        if detail:
            lines_height += 1 + normal.height()
        return QSize(width, lines_height + self._VERTICAL_PADDING * 2)


class CompletionService(QObject):
    """Ejecuta ``pipoc --ll1-expected`` sin bloquear el hilo de la interfaz."""

    ready = Signal(int, tuple)
    unavailable = Signal(str)

    def __init__(self, parent=None):
        """Inicializa la consulta activa y la propiedad de su archivo temporal."""
        super().__init__(parent)
        self._request_id = 0
        self._process: QProcess | None = None
        self._temporary: Path | None = None

    def request(self, source_prefix: str, project_root: Path) -> int | None:
        """Escribe el prefijo y consulta directamente al analizador C en Linux/WSL.

        El IDE y Pipoc comparten el entorno y las rutas, incluido el temporal.
        La tabla LL(1) permanece en Pipoc; Python envía el prefijo y recibe un
        terminal por línea, sin convertir rutas ni iniciar otro sistema.
        """
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
        errors = bytes(process.readAllStandardError()).decode("utf-8", "replace")
        expected = tuple(line.strip() for line in output.splitlines() if line.strip())
        self._process = None
        process.deleteLater()
        self._remove_temporary()
        if exit_code == 2:
            self.unavailable.emit("Pipoc no reconoce --ll1-expected. Actualizá el compilador con make all.")
        elif exit_code != 0 and not (exit_code == 1 and errors.startswith("ERROR:")):
            self.unavailable.emit(errors.strip() or f"Pipoc terminó con código {exit_code}.")
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
    """Coordina consultas C y una lista integrada en la zona de texto.

    La lista es un widget hijo, no una ventana Qt.Popup: WSLg no tiene que
    transferirle foco ni capturar el teclado para mostrarla. El cursor permanece
    en el editor y este controlador procesa las teclas para elegir una opción.
    """

    unavailable = Signal(str)

    def __init__(self, editor, parent=None):
        """Conecta el editor, los temporizadores y el popup con el servicio de Pipoc."""
        super().__init__(parent)
        self.editor = editor
        self._viewport = editor.viewport()
        self._window = editor.window()
        self.service = CompletionService(self)
        self.model = QStandardItemModel(self)
        self.popup = QListView(editor.viewport())
        self.popup.setObjectName("completionPopup")
        self.popup.setAccessibleName("Sugerencias de Piper")
        self.popup.setModel(self.model)
        self.popup.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.popup.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.popup.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.popup.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.popup.setUniformItemSizes(False)
        self.popup.setItemDelegate(CompletionItemDelegate(self.popup))
        self.popup.clicked.connect(self._choose)
        self.popup.activated.connect(self._choose)
        self.popup.hide()
        self.service.ready.connect(self._show)
        self.service.unavailable.connect(self.unavailable.emit)
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(180)
        self.timer.timeout.connect(self._request_automatic)
        # textChanged y cursorPositionChanged no describen necesariamente el
        # estado final en el mismo orden. Este segundo temporizador reúne ambas
        # señales al terminar la pulsación; solo entonces empieza el debounce.
        self._settle_timer = QTimer(self)
        self._settle_timer.setSingleShot(True)
        self._settle_timer.setInterval(0)
        self._settle_timer.timeout.connect(self._settle_edit)
        self._text_edited = False
        self._generation = 0
        self.enabled = False
        self.project_root = Path.cwd()
        self._configured_document = None
        self._pending_id: int | None = None
        self._pending_state: _EditorState | None = None
        self._pending_generation = -1
        self._pending_explicit = False
        self._pending_allow_empty = False
        self._visible_state: _EditorState | None = None
        self._trigger_all_candidates = False
        editor.textChanged.connect(self.schedule)
        editor.cursorPositionChanged.connect(self._cursor_changed)
        editor.selectionChanged.connect(self._cursor_changed)
        editor.completionRequested.connect(self.request_now)
        editor.completionTriggered.connect(self.schedule_trigger)
        # El filtro de aplicación permite cerrar al hacer clic en otra zona del
        # IDE, sin interpretar una notificación de foco del compositor como edición.
        QApplication.instance().installEventFilter(self)
        editor.verticalScrollBar().valueChanged.connect(self.hide)
        editor.horizontalScrollBar().valueChanged.connect(self.hide)

    def configure(self, enabled: bool, project_root: Path):
        """Activa sugerencias solo para un buffer Piper y fija el Pipoc cercano."""
        resolved_root = Path(project_root).resolve()
        document = self.editor.document()
        if (self.enabled == enabled and self.project_root == resolved_root
                and self._configured_document is document):
            return
        self.enabled = enabled
        self.project_root = resolved_root
        self._configured_document = document
        self.hide()
        self.service.cancel()

    def schedule(self):
        """Invalida respuestas inmediatamente y espera el cursor final de la edición."""
        self._invalidate()
        self._text_edited = True
        self._settle_timer.start()

    def _cursor_changed(self):
        """Mover o seleccionar invalida la lista; no abre sugerencias por sí solo."""
        self._invalidate()
        self._settle_timer.start()

    def schedule_trigger(self):
        """Permite sugerencias sin fragmento después de un delimitador Piper."""
        if not self.enabled or self.editor.isReadOnly():
            return
        state = self._state()
        if state.position != state.anchor or state.context.suppressed:
            return
        self._trigger_all_candidates = True
        self._settle_timer.start()

    def _settle_edit(self):
        """Programa la consulta después de reunir los eventos de una misma edición.

        Un movimiento sin cambios solo cierra la lista. Borrar toda la palabra,
        seleccionar texto o entrar a un comentario nunca inicia una consulta
        automática con fragmento vacío.
        """
        edited, self._text_edited = self._text_edited, False
        self.timer.stop()
        if not edited or not self.enabled or self.editor.isReadOnly():
            return
        state = self._state()
        if (state.position == state.anchor and not state.context.suppressed
                and (state.context.fragment or self._trigger_all_candidates)):
            self.timer.start()

    def request_now(self):
        """Solicita opciones incluso sin fragmento mediante Ctrl+Espacio."""
        self.hide()
        self._request(explicit=True)

    def _request_automatic(self):
        """Consulta el estado vigente al terminar la pausa de escritura."""
        allow_empty = self._trigger_all_candidates
        self._trigger_all_candidates = False
        self._request(explicit=False, allow_empty=allow_empty)

    def _request(self, explicit: bool, allow_empty: bool = False):
        """Liga la respuesta asíncrona a una foto y generación exactas del editor."""
        if not self.enabled or self.editor.isReadOnly():
            return
        state = self._state()
        context = state.context
        if (state.position != state.anchor or context.suppressed
                or (not explicit and not allow_empty and not context.fragment)):
            return
        self._pending_state = state
        self._pending_generation = self._generation
        self._pending_explicit = explicit
        self._pending_allow_empty = allow_empty
        self._pending_id = self.service.request(
            context.parse_prefix, self.project_root,
        )

    def _show(self, request_id: int, expected: tuple[str, ...]):
        """Descarta respuestas obsoletas y abre el popup junto al cursor."""
        if (request_id != self._pending_id or not self.enabled
                or self.editor.isReadOnly()
                or self._pending_generation != self._generation):
            return
        state = self._state()
        if state != self._pending_state:
            return
        current = state.context
        if (current.suppressed
                or (not self._pending_explicit and not self._pending_allow_empty
                    and not current.fragment)):
            return
        candidates = completion_items(
            expected, current.fragment, self.editor.toPlainText(),
        )
        if not candidates:
            self.hide()
            return
        self.model.clear()
        for candidate in candidates:
            item = QStandardItem(candidate.text)
            item.setEditable(False)
            item.setData(candidate.detail, DETAIL_ROLE)
            item.setToolTip(candidate.detail)
            self.model.appendRow(item)
        self.popup.setFont(self.editor.font())
        self.popup.setCurrentIndex(self.model.index(0, 0))
        self._visible_state = state
        self._position_popup()
        self.popup.show()
        self.popup.raise_()

    def _position_popup(self):
        """Ajusta la lista al área visible, debajo o encima del cursor.

        Las coordenadas pertenecen al viewport. Se limita a nueve filas y al
        tamaño disponible para no salir de la ventana ni tapar otros paneles.
        """
        viewport = self.editor.viewport()
        cursor = self.editor.cursorRect()
        metrics = self.popup.fontMetrics()
        content_width = max((metrics.horizontalAdvance(self.model.item(row).text())
                             for row in range(self.model.rowCount())), default=240)
        detail_width = max((metrics.horizontalAdvance(self.model.item(row).data(DETAIL_ROLE) or "")
                            for row in range(self.model.rowCount())), default=240)
        width = min(max(1, viewport.width() - 8), max(280, min(760, max(content_width, detail_width) + 38)))
        wanted_height = sum(max(1, self.popup.sizeHintForRow(row))
                            for row in range(min(9, self.model.rowCount()))) + 6
        below = max(0, viewport.height() - cursor.bottom() - 5)
        above = max(0, cursor.top() - 4)
        open_below = below >= wanted_height or below >= above
        height = min(wanted_height, max(1, below if open_below else above))
        x = max(0, min(cursor.left(), viewport.width() - width - 4))
        y = cursor.bottom() + 2 if open_below else cursor.top() - height - 2
        self.popup.setGeometry(x, max(0, y), width, height)

    def _choose(self, index):
        """Acepta una fila por mouse o teclado, usando la misma validación de estado."""
        if index.isValid():
            self._insert(str(index.data(Qt.ItemDataRole.DisplayRole)))

    def _move_choice(self, delta: int):
        """Recorre opciones sin mover el cursor de código ni transferir el foco."""
        count = self.model.rowCount()
        if count:
            row = (self.popup.currentIndex().row() + delta) % count
            index = self.model.index(row, 0)
            self.popup.setCurrentIndex(index)
            self.popup.scrollTo(index)

    def _insert(self, completion: str):
        """Reemplaza solamente el fragmento vigente y conserva Deshacer de Qt."""
        if (not self.enabled or self.editor.isReadOnly()
                or self._visible_state != self._state()):
            return
        context = self._context()
        cursor = self.editor.textCursor()
        for _ in context.fragment:
            cursor.movePosition(
                QTextCursor.MoveOperation.PreviousCharacter,
                QTextCursor.MoveMode.KeepAnchor,
            )
        cursor.insertText(completion)
        self.editor.setTextCursor(cursor)
        # Insertar una opción también emite textChanged. Se cierra ese ciclo
        # para no volver a sugerir automáticamente la palabra recién aceptada.
        self.hide()

    def _state(self) -> _EditorState:
        """Captura identidad, revisión, selección y contexto sin modificar el cursor."""
        cursor = self.editor.textCursor()
        document = self.editor.document()
        return _EditorState(document, document.revision(), cursor.position(),
                            cursor.anchor(), self._context())

    def _context(self) -> CompletionContext:
        """Obtiene el texto anterior al cursor con los saltos normalizados para Pipoc."""
        cursor = QTextCursor(self.editor.textCursor())
        cursor.setPosition(0, QTextCursor.MoveMode.KeepAnchor)
        before = cursor.selectedText().replace("\u2029", "\n")
        return completion_context(before)

    def _invalidate(self):
        """Descarta peticiones y opciones que pertenecen a otro estado de edición.

        La generación invalida también respuestas que regresan al mismo texto
        después de borrar y deshacer. La lista vacía nunca puede insertar datos
        pertenecientes a una consulta anterior.
        """
        self._generation += 1
        self._pending_id = None
        self._pending_state = None
        self._pending_explicit = False
        self._pending_allow_empty = False
        self._visible_state = None
        self._trigger_all_candidates = False
        self.popup.hide()
        self.model.clear()

    def hide(self):
        """Cierra la lista y cancela futuras aperturas de la interacción anterior."""
        self._invalidate()
        self.timer.stop()
        self._settle_timer.stop()
        self._text_edited = False

    def eventFilter(self, watched, event):
        """Coordina teclado y clics sin abrir ni enfocar una ventana emergente.

        Arriba/Abajo eligen fila, Enter/Tab aceptan y Esc descarta. Las otras
        teclas siguen al editor. Los clics fuera y la desactivación real de la
        ventana cancelan respuestas; FocusOut por sí solo no borra predicciones.
        """
        if watched is self.editor and event.type() == QEvent.Type.KeyPress:
            if event.key() == Qt.Key.Key_Escape:
                self.hide()
                return True
            if self.popup.isVisible() and not event.modifiers() & (
                    Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier):
                if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter, Qt.Key.Key_Tab):
                    self._choose(self.popup.currentIndex())
                    return True
                if event.key() in (Qt.Key.Key_Up, Qt.Key.Key_Down, Qt.Key.Key_Backtab):
                    self._move_choice(1 if event.key() == Qt.Key.Key_Down else -1)
                    return True
        if event.type() == QEvent.Type.MouseButtonPress and isinstance(watched, QWidget):
            if watched is not self.popup and not self.popup.isAncestorOf(watched):
                self.hide()
        if watched is self._viewport and event.type() == QEvent.Type.Resize:
            if self.popup.isVisible():
                self._position_popup()
        if (watched is self._window
                and event.type() in (QEvent.Type.WindowDeactivate, QEvent.Type.Hide)):
            self.hide()
        return super().eventFilter(watched, event)

    def stop(self):
        """Cancela debounce y proceso auxiliar al cerrar la aplicación."""
        QApplication.instance().removeEventFilter(self)
        self.hide()
        self.service.stop()
