"""Coordinación de documentos: buffers, diálogos y llamadas a la capa de archivos.

No contiene compilación ni asistencia de lenguaje. Cada acción que puede perder
cambios pide Guardar/Descartar/Cancelar; cambiar de buffer o refrescar el árbol
no pierde contenido. Las rutas se resuelven para no abrir dos buffers del mismo
archivo mediante alias distintos.
"""

import os
from pathlib import Path

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import QFileDialog, QMessageBox

from ..infrastructure.files import DiskChangedError, TextFileError, disk_digest, read_text_file, write_text_file
from ..models.document import Document
from .documents import EditorSession, create_session
from .theme import load_theme, monospace_font


FILE_FILTERS = "Piper (*.rwt);;ASM PORYGON (*.asm);;Salidas de texto (*.bundles *.mem *.json *.txt *.log);;Todos los archivos (*)"


class DocumentController(QObject):
    """Mantiene sesiones independientes y presenta una sola en el editor compartido."""

    activeChanged = Signal()
    documentsChanged = Signal()
    message = Signal(str)
    errorOccurred = Signal(str)
    fileSaved = Signal()

    def __init__(self, editor, parent):
        """Asocia el editor y el dueño de los diálogos; todavía no abre archivos."""
        super().__init__(parent)
        self.editor = editor
        self.owner = parent
        self.sessions: list[EditorSession] = []
        self.active: EditorSession | None = None
        self.project = None
        self.colors = load_theme("rowlet-oscuro")
        self.font_size = 14
        self._untitled_count = 0

    def _add(self, info: Document, text: str) -> EditorSession:
        """Crea un buffer con historial propio y conecta su indicador de cambios."""
        session = create_session(info, text, self.colors, self.font_size, self)
        self.sessions.append(session)
        session.buffer.modificationChanged.connect(lambda modified, item=session: self._changed(item))
        self.activate(info.id)
        return session

    def _changed(self, session):
        """Actualiza la lista lateral y, si corresponde, la cabecera activa."""
        self.documentsChanged.emit()
        if session is self.active:
            self.activeChanged.emit()

    def new_document(self):
        """Abre un documento vacío; crear no escribe todavía en el disco."""
        self._untitled_count += 1
        return self._add(Document(f"sin-titulo-{self._untitled_count}.rwt"), "")

    def open_file(self, path: Path):
        """Reutiliza un buffer existente o lee texto, rechazando binarios sin alterarlos."""
        try:
            path = Path(path).expanduser().resolve()
            existing = next((item for item in self.sessions if item.info.path == path), None)
            if existing:
                self.activate(existing.info.id)
                return existing
            content = read_text_file(path)
            generated = bool(self.project and self.project.is_generated(path))
            info = Document(path.name, path, content.encoding, content.newline, content.digest,
                            read_only=generated or not os.access(path, os.W_OK), generated=generated)
            session = self._add(info, content.text)
            self.message.emit(f"Abierto: {path}")
            return session
        except (OSError, TextFileError) as error:
            self.errorOccurred.emit(str(error))
            return None

    def _capture_view(self, session):
        """Recuerda selección y desplazamientos antes de mostrar otro documento."""
        cursor = self.editor.textCursor()
        session.position, session.anchor = cursor.position(), cursor.anchor()
        session.vertical_scroll = self.editor.verticalScrollBar().value()
        session.horizontal_scroll = self.editor.horizontalScrollBar().value()

    def _restore_view(self, session):
        """Restaura una selección válida aunque una recarga haya acortado el texto."""
        maximum = session.buffer.characterCount() - 1
        cursor = QTextCursor(session.buffer)
        cursor.setPosition(min(session.anchor, maximum))
        cursor.setPosition(min(session.position, maximum), QTextCursor.MoveMode.KeepAnchor)
        self.editor.setTextCursor(cursor)
        self.editor.verticalScrollBar().setValue(session.vertical_scroll)
        self.editor.horizontalScrollBar().setValue(session.horizontal_scroll)

    def activate(self, document_id: str):
        """Intercambia QTextDocument sin reconstruir texto ni borrar Deshacer."""
        session = next((item for item in self.sessions if item.info.id == document_id), None)
        if session is None:
            return
        if session is not self.active:
            if self.active:
                self._capture_view(self.active)
            self.active = session
            self.editor.setDocument(session.buffer)
            self.editor.setReadOnly(session.info.read_only)
            self._restore_view(session)
        self.documentsChanged.emit()
        self.activeChanged.emit()
        self.editor.setFocus()

    def choose_file(self):
        """Pregunta por un archivo; cancelar conserva el documento activo."""
        initial = self.project.root if self.project else Path.home()
        path, _ = QFileDialog.getOpenFileName(self.owner, "Abrir archivo", str(initial), FILE_FILTERS)
        if path:
            self.open_file(Path(path))

    def save_as(self, session=None):
        """Pregunta un destino y evita sobrescribir otro buffer abierto o un generado.

        El diálogo nativo confirma destinos existentes. Guardar como cambia la
        identidad de ruta del buffer actual, dejando intacto el archivo anterior.
        """
        session = session or self.active
        if not session:
            return False
        info = session.info
        directory = (info.path.parent if info.path and not info.read_only else
                     self.project.root if self.project else Path.home())
        path, _ = QFileDialog.getSaveFileName(self.owner, "Guardar como", str(directory / info.name), FILE_FILTERS)
        if not path:
            return False
        return self.save(session, Path(path).expanduser().resolve())

    def save(self, session=None, target: Path | None = None) -> bool:
        """Guarda el buffer pedido; devuelve False ante error o cancelación.

        Los metadatos y la marca «sin guardar» solo cambian después de commit.
        Si el destino cambió en otra aplicación, se exige una confirmación
        adicional antes de reemplazarlo. Los archivos de build se consultan y
        se pueden copiar fuera de esa carpeta mediante Guardar como.
        """
        session = session or self.active
        if not session:
            return False
        info = session.info
        if target is None and (info.path is None or info.read_only):
            return self.save_as(session)
        target = Path(target or info.path).resolve()
        if self.project and self.project.is_generated(target):
            self.errorOccurred.emit("Elegí un destino fuera de build para guardar una copia editable.")
            return False
        if any(item is not session and item.info.path == target for item in self.sessions):
            self.errorOccurred.emit("El destino ya está abierto en otro documento. Cerralo antes de reemplazarlo.")
            return False
        if info.read_only and target == info.path:
            self.errorOccurred.emit("El archivo es de solo lectura. Elegí otro destino con Guardar como.")
            return False
        try:
            expected = info.disk_digest if target == info.path else disk_digest(target)
            try:
                digest = write_text_file(target, session.buffer.toPlainText(), info.encoding, info.newline, expected)
            except DiskChangedError:
                answer = QMessageBox.question(self.owner, "El archivo cambió en disco",
                    f"{target}\n\nCambió fuera del IDE o fue eliminado. ¿Querés reemplazarlo con el contenido abierto?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
                if answer != QMessageBox.StandardButton.Yes:
                    return False
                digest = write_text_file(target, session.buffer.toPlainText(), info.encoding, info.newline, expected, force=True)
        except (OSError, UnicodeError, TextFileError) as error:
            self.errorOccurred.emit(f"No se pudo guardar {target}: {error}")
            return False
        info.path, info.name, info.disk_digest = target, target.name, digest
        info.generated, info.read_only = False, False
        session.highlighter.language = info.language
        session.highlighter.rehighlight()
        session.buffer.setModified(False)
        if session is self.active:
            self.editor.setReadOnly(False)
        self.documentsChanged.emit()
        self.activeChanged.emit()
        self.message.emit(f"Guardado: {target}")
        self.fileSaved.emit()
        return True

    def _maybe_save(self, session) -> bool:
        """Pide Guardar/Descartar/Cancelar exclusivamente si existen cambios reales."""
        if not session.buffer.isModified():
            return True
        self.activate(session.info.id)
        answer = QMessageBox.question(self.owner, "Cambios sin guardar",
            f"¿Querés guardar los cambios de {session.info.name}?",
            QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Save)
        if answer == QMessageBox.StandardButton.Save:
            return self.save(session)
        return answer == QMessageBox.StandardButton.Discard

    def close_active(self):
        """Cierra el documento tras resolver sus cambios; mantiene un buffer si era el último."""
        session = self.active
        if not session or not self._maybe_save(session):
            return False
        index = self.sessions.index(session)
        self.sessions.remove(session)
        self.active = None
        if self.sessions:
            self.activate(self.sessions[min(index, len(self.sessions) - 1)].info.id)
        else:
            self.new_document()
        session.buffer.deleteLater()
        return True

    def can_close_window(self) -> bool:
        """Resuelve todos los buffers; cancelar deja abierta la ventana y sus documentos."""
        for session in list(self.sessions):
            if not self._maybe_save(session):
                return False
        return True

    def reload_active(self):
        """Recarga explícitamente desde disco; advierte antes de descartar ediciones."""
        session = self.active
        if not session or session.info.path is None:
            return False
        if session.buffer.isModified():
            answer = QMessageBox.question(self.owner, "Recargar archivo",
                "Se descartarán los cambios de este documento. ¿Querés continuar?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
            if answer != QMessageBox.StandardButton.Yes:
                return False
        try:
            content = read_text_file(session.info.path)
        except (OSError, TextFileError) as error:
            self.errorOccurred.emit(str(error))
            return False
        self._capture_view(session)
        info = session.info
        info.encoding, info.newline, info.disk_digest = content.encoding, content.newline, content.digest
        session.buffer.setPlainText(content.text)
        session.buffer.setModified(False)
        self._restore_view(session)
        self.activeChanged.emit()
        self.message.emit(f"Recargado: {info.path}")
        return True

    def cycle(self, direction: int):
        """Navega los buffers por orden de apertura, continuando al llegar al borde."""
        if self.active and self.sessions:
            index = (self.sessions.index(self.active) + direction) % len(self.sessions)
            self.activate(self.sessions[index].info.id)

    def apply_theme(self, colors):
        """Repinta todos los buffers, incluso los que no están visibles."""
        self.colors = colors
        for session in self.sessions:
            session.highlighter.apply_theme(colors)

    def set_font_size(self, size: int):
        """Actualiza la fuente compartida por la vista y por cada layout de documento."""
        self.font_size = size
        for session in self.sessions:
            session.buffer.setDefaultFont(monospace_font(size))
        self.editor.set_code_font_size(size)
