"""Ventana del IDE: composición visual y coordinación de acciones del usuario.

DocumentController conserva los buffers y resuelve lectura/guardado. CodeEditor
edita el buffer activo; los resaltadores solo aplican colores. Esta ventana no
construye comandos de compilación ni calcula diagnósticos, sugerencias o clones.
"""

from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QAction, QIcon, QPixmap
from PySide6.QtWidgets import (
    QApplication, QComboBox, QFileDialog, QFrame, QHBoxLayout, QInputDialog,
    QLabel, QLineEdit, QMainWindow, QMenu, QMessageBox, QPlainTextEdit,
    QPushButton, QSizePolicy, QSplitter, QToolButton, QVBoxLayout, QWidget,
)

from ..editor.code_editor import CodeEditor
from ..models.project import Project
from .appearance_panel import AppearancePanel
from .document_controller import DocumentController
from .explorer import Explorer
from .icons import line_icon
from .output_panel import OutputPanel
from .search_bar import SearchBar
from .theme import load_theme, monospace_font, palette_for, resource_path, stylesheet


class MainWindow(QMainWindow):
    """Una vista de código compartida, sin pestañas ni modos de edición.

    La lista Abiertos permite cambiar de buffer sin perder su historial. Las
    acciones de Ver son la fuente del estado de visibilidad de cada panel.
    La apariencia es de sesión y no escribe archivos de configuración.
    """

    def __init__(self, project_root: Path | None = None):
        """Construye la ventana y abre un buffer vacío; la carpeta inicial es opcional."""
        super().__init__()
        self.setWindowTitle("Piper IDE")
        self.setWindowIcon(QIcon(resource_path("images/rowlet.png")))
        self.resize(1440, 940)
        self.setMinimumSize(1120, 680)
        self.theme_id = "rowlet-oscuro"
        self.code_font_size = 14
        self.project = None
        self.actions = {}
        root = QWidget()
        root.setObjectName("workspace")
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self._topbar())

        workspace = QHBoxLayout()
        workspace.setSpacing(0)
        self.workspace_splitter = QSplitter(Qt.Orientation.Horizontal)
        self.workspace_splitter.setChildrenCollapsible(False)
        self.workspace_splitter.setHandleWidth(1)
        self.explorer = Explorer()
        self.workspace_splitter.addWidget(self.explorer)
        self.editor_splitter = QSplitter(Qt.Orientation.Vertical)
        self.editor_splitter.setChildrenCollapsible(False)
        self.editor_splitter.setHandleWidth(3)
        editor_area = QWidget()
        editor_layout = QVBoxLayout(editor_area)
        editor_layout.setContentsMargins(0, 0, 0, 0)
        editor_layout.setSpacing(0)
        editor_layout.addWidget(self._editor_heading())
        self.search_bar = SearchBar()
        self.search_bar.hide()
        editor_layout.addWidget(self.search_bar)
        self.editor = CodeEditor()
        self.editor.setPlaceholderText("Escribí un programa o abrí un archivo para comenzar.")
        editor_layout.addWidget(self.editor, 1)
        self.editor_splitter.addWidget(editor_area)
        self.output_panel = OutputPanel()
        self.editor_splitter.addWidget(self.output_panel)
        self.editor_splitter.setStretchFactor(0, 1)
        self.editor_splitter.setSizes([620, 215])
        self.workspace_splitter.addWidget(self.editor_splitter)
        self.workspace_splitter.setStretchFactor(1, 1)
        self.workspace_splitter.setSizes([260, 1180])
        workspace.addWidget(self.workspace_splitter, 1)
        self.appearance = AppearancePanel()
        self.appearance.hide()
        workspace.addWidget(self.appearance)
        layout.addLayout(workspace, 1)
        layout.addWidget(self._statusbar())

        # El controlador es el padre de los QTextDocument. Sustituir el buffer
        # del editor nunca destruye los documentos que permanecen abiertos.
        self.documents = DocumentController(self.editor, self)
        self._menus()
        self._connect_components()
        self.search_timer = QTimer(self)
        self.search_timer.setSingleShot(True)
        self.search_timer.setInterval(150)
        self.search_timer.timeout.connect(self._refresh_search)
        self.set_theme(self.theme_id)
        self.documents.new_document()
        if project_root is not None:
            self.open_folder(project_root)

    def _connect_components(self):
        """Conecta señales entre componentes sin hacer que se conozcan entre sí."""
        self.explorer.newRequested.connect(self.documents.new_document)
        self.explorer.folderRequested.connect(self.open_folder)
        self.explorer.fileRequested.connect(self.documents.open_file)
        self.explorer.documentRequested.connect(self.documents.activate)
        self.explorer.errorOccurred.connect(self._show_error)
        self.explorer.refreshed.connect(lambda: self._message("Explorador actualizado; documentos conservados."))
        self.documents.activeChanged.connect(self._active_changed)
        self.documents.documentsChanged.connect(self._update_documents)
        self.documents.errorOccurred.connect(self._show_error)
        self.documents.message.connect(self._message)
        self.documents.fileSaved.connect(self.explorer.refresh)
        self.editor.cursorPositionChanged.connect(self._update_cursor)
        self.editor.undoAvailable.connect(lambda available: self._update_actions())
        self.editor.redoAvailable.connect(lambda available: self._update_actions())
        self.editor.textChanged.connect(self._schedule_search)
        self.search_bar.queryChanged.connect(self._schedule_search)
        self.search_bar.findRequested.connect(self.find_next)
        self.search_bar.closeRequested.connect(self.close_search)
        self.appearance_button.toggled.connect(self.appearance.setVisible)
        self.appearance.closeRequested.connect(lambda: self.appearance_button.setChecked(False))
        self.appearance.themeChanged.connect(self.set_theme)
        self.appearance.fontSizeChanged.connect(self.set_code_font_size)
        self.output_panel.closeRequested.connect(lambda: self.panel_action.setChecked(False))
        self.save_button.clicked.connect(lambda: self.documents.save())

    def _topbar(self):
        """Construye la única fila de controles; Compilar queda pendiente del punto 4."""
        bar = QFrame()
        bar.setObjectName("topbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(16, 10, 16, 10)
        row.setSpacing(8)
        image = QLabel()
        image.setPixmap(QPixmap(resource_path("images/rowlet.png")).scaled(
            34, 38, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
        row.addWidget(image)
        brand = QLabel("Piper IDE")
        brand.setObjectName("brand")
        row.addWidget(brand)
        row.addSpacing(8)
        self.menu_buttons = {}
        for name in ("Archivo", "Editar", "Ver"):
            button = QPushButton(name)
            row.addWidget(button)
            self.menu_buttons[name] = button
        row.addStretch()
        self.folder_label = QLabel("Sin carpeta")
        self.folder_label.setProperty("role", "muted")
        self.folder_label.setMaximumWidth(220)
        row.addWidget(self.folder_label)
        row.addStretch()
        self.appearance_button = QToolButton()
        self.appearance_button.setCheckable(True)
        self.appearance_button.setAccessibleName("Apariencia")
        self.appearance_button.setToolTip("Apariencia")
        row.addWidget(self.appearance_button)
        self.save_button = QPushButton("Guardar  Ctrl+S")
        self.save_button.setToolTip("Guardar el archivo activo")
        row.addWidget(self.save_button)
        target_label = QLabel("Salida")
        target_label.setProperty("role", "muted")
        row.addWidget(target_label)
        self.compile_target = QComboBox()
        self.compile_target.setAccessibleName("Salida de compilación")
        for title, command in (("Binario (.bin)", "bin"), ("ASM (.asm)", "asm"),
                               ("Bundles (.bundles)", "sch"), ("Memoria (.mem)", "mem"),
                               ("AST y símbolos", "ins"), ("Todo", "all")):
            self.compile_target.addItem(title, command)
        self.compile_target.setToolTip("La compilación se conectará en el punto 4")
        row.addWidget(self.compile_target)
        self.compile_button = QPushButton("Compilar  F5")
        self.compile_button.setObjectName("compileButton")
        self.compile_button.setEnabled(False)
        self.compile_button.setToolTip("La conexión con pipoc está pendiente")
        row.addWidget(self.compile_button)
        return bar

    def _editor_heading(self):
        """Reserva las etiquetas del documento activo, sin pestañas de archivos."""
        header = QFrame()
        header.setObjectName("editorHeading")
        row = QHBoxLayout(header)
        row.setContentsMargins(24, 12, 24, 12)
        column = QVBoxLayout()
        column.setSpacing(4)
        self.origin_label = QLabel("ARCHIVO ACTIVO")
        self.origin_label.setProperty("role", "eyebrow")
        column.addWidget(self.origin_label)
        self.filename_label = QLabel()
        self.filename_label.setObjectName("activeFilename")
        self.filename_label.setTextFormat(Qt.TextFormat.PlainText)
        self.filename_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        column.addWidget(self.filename_label)
        self.path_label = QLabel()
        self.path_label.setProperty("role", "muted")
        self.path_label.setTextFormat(Qt.TextFormat.PlainText)
        self.path_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        column.addWidget(self.path_label)
        row.addLayout(column, 1)
        self.modified_label = QLabel("sin guardar")
        self.modified_label.setProperty("role", "error")
        row.addWidget(self.modified_label)
        self.readonly_label = QLabel("solo lectura")
        self.readonly_label.setProperty("role", "accent")
        row.addWidget(self.readonly_label)
        self.kind_label = QLabel()
        self.kind_label.setProperty("role", "eyebrow")
        row.addWidget(self.kind_label)
        return header

    def _statusbar(self):
        """Muestra mensajes de operaciones reales y datos del documento activo."""
        bar = QFrame()
        bar.setObjectName("statusbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(16, 5, 16, 5)
        row.setSpacing(18)
        self.message_label = QLabel("Listo")
        self.message_label.setProperty("role", "accent")
        self.message_label.setTextFormat(Qt.TextFormat.PlainText)
        self.message_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        row.addWidget(self.message_label, 1)
        self.cursor_label = QLabel()
        self.encoding_label = QLabel()
        self.newline_label = QLabel()
        self.language_label = QLabel()
        for label in (self.cursor_label, QLabel("Espacios: 4"), self.encoding_label, self.newline_label, self.language_label):
            row.addWidget(label)
        return bar

    def _action(self, menu, key, title, shortcut, callback):
        """Registra una acción de ventana sin propagar el bool de triggered al callback."""
        action = QAction(title, self)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(lambda checked=False: callback())
        menu.addAction(action)
        self.actions[key] = action
        return action

    def _menus(self):
        """Asigna comandos de archivos, edición y navegación con atajos estándar."""
        file_menu = QMenu(self)
        for key, title, shortcut, callback in (
            ("new", "Nuevo archivo", "Ctrl+N", self.documents.new_document),
            ("open", "Abrir archivo…", "Ctrl+O", self.documents.choose_file),
            ("folder", "Abrir carpeta…", "Ctrl+Shift+O", self.open_folder),
            ("save", "Guardar", "Ctrl+S", self.documents.save),
            ("save_as", "Guardar como…", "Ctrl+Shift+S", self.documents.save_as),
            ("reload", "Recargar desde disco", None, self.documents.reload_active),
            ("close", "Cerrar archivo", "Ctrl+W", self.documents.close_active),
        ):
            self._action(file_menu, key, title, shortcut, callback)
        file_menu.addSeparator()
        self._action(file_menu, "exit", "Salir", "Ctrl+Q", self.close)
        self.menu_buttons["Archivo"].setMenu(file_menu)
        edit_menu = QMenu(self)
        for key, title, shortcut, callback in (
            ("undo", "Deshacer", "Ctrl+Z", lambda: self._edit("undo")),
            ("redo", "Rehacer", "Ctrl+Shift+Z", lambda: self._edit("redo")),
            ("cut", "Cortar", "Ctrl+X", lambda: self._edit("cut")),
            ("copy", "Copiar", "Ctrl+C", lambda: self._edit("copy")),
            ("paste", "Pegar", "Ctrl+V", lambda: self._edit("paste")),
            ("select", "Seleccionar todo", "Ctrl+A", lambda: self._edit("selectAll")),
            ("search", "Buscar…", "Ctrl+F", self.open_search),
            ("next_match", "Coincidencia siguiente", "F3", self.find_next),
            ("previous_match", "Coincidencia anterior", "Shift+F3", lambda: self.find_next(True)),
            ("line", "Ir a línea…", "Ctrl+G", self.go_to_line),
        ):
            self._action(edit_menu, key, title, shortcut, callback)
        self.menu_buttons["Editar"].setMenu(edit_menu)
        view_menu = QMenu(self)
        self.explorer_action = QAction("Explorador", self)
        self.panel_action = QAction("Panel inferior", self)
        for action, widget in ((self.explorer_action, self.explorer), (self.panel_action, self.output_panel)):
            action.setCheckable(True)
            action.setChecked(True)
            action.toggled.connect(widget.setVisible)
            view_menu.addAction(action)
        view_menu.addSeparator()
        for index, title in enumerate(("Problemas", "Consola", "Clones")):
            self._action(view_menu, title, title, None, lambda value=index: self.show_output_page(value))
        view_menu.addSeparator()
        self._action(view_menu, "next_document", "Documento siguiente", "Ctrl+PageDown", lambda: self.documents.cycle(1))
        self._action(view_menu, "previous_document", "Documento anterior", "Ctrl+PageUp", lambda: self.documents.cycle(-1))
        self.menu_buttons["Ver"].setMenu(view_menu)

    def _edit(self, command):
        """Aplica edición al campo enfocado; la consola conserva su solo lectura."""
        widget = QApplication.focusWidget()
        target = widget if isinstance(widget, (QPlainTextEdit, QLineEdit)) else self.editor
        getattr(target, command)()

    def open_folder(self, path: Path | None = None):
        """Cambia la raíz del explorador conservando todos los buffers abiertos."""
        if path is None:
            chosen = QFileDialog.getExistingDirectory(self, "Abrir carpeta", str(self.project.root if self.project else Path.cwd()))
            if not chosen:
                return False
            path = Path(chosen)
        path = Path(path).expanduser().resolve()
        if not path.is_dir():
            self._show_error(f"La carpeta no existe: {path}")
            return False
        self.project = Project(path)
        self.documents.project = self.project
        self.explorer.set_project(self.project)
        self.folder_label.setText("Carpeta: " + path.name)
        self.folder_label.setToolTip(str(path))
        for session in self.documents.sessions:
            if session.info.path and self.project.is_generated(session.info.path):
                session.info.generated = session.info.read_only = True
        if self.documents.active:
            self.editor.setReadOnly(self.documents.active.info.read_only)
        self._update_documents()
        self._active_changed()
        self._message(f"Carpeta abierta: {path}")
        return True

    def _update_documents(self):
        """Sincroniza la lista de abiertos y los comandos de edición disponibles."""
        active = self.documents.active
        self.explorer.set_documents(self.documents.sessions, active.info.id if active else "")
        self._update_actions()

    def _active_changed(self):
        """Refleja ruta, formato y permisos del documento que ya muestra el editor."""
        session = self.documents.active
        if not session:
            return
        info = session.info
        self.origin_label.setText("ARCHIVO GENERADO" if info.generated else "ARCHIVO ACTIVO")
        self.filename_label.setText(info.name)
        self.filename_label.setToolTip(info.name)
        self.path_label.setText(str(info.path) if info.path else "Nuevo archivo · elegí una ruta al guardar")
        self.path_label.setToolTip(self.path_label.text())
        self.modified_label.setVisible(session.buffer.isModified() or info.path is None)
        self.readonly_label.setVisible(info.read_only)
        self.kind_label.setText(info.language_label)
        self.encoding_label.setText(info.encoding.upper())
        self.newline_label.setText({"\n": "LF", "\r\n": "CRLF", "\r": "CR"}[info.newline])
        self.language_label.setText(info.language_label)
        self.setWindowTitle(f"{'● ' if session.buffer.isModified() else ''}{info.name} · Piper IDE")
        self._update_cursor()
        self._update_actions()
        self._schedule_search()

    def _update_actions(self):
        """Evita operaciones de escritura sobre generados sin bloquear Guardar como."""
        active = self.documents.active
        writable = bool(active and not active.info.read_only)
        save = bool(writable and (active.buffer.isModified() or active.info.path is None))
        self.save_button.setEnabled(save)
        self.actions["save"].setEnabled(save)
        self.actions["reload"].setEnabled(bool(active and active.info.path))
        self.actions["undo"].setEnabled(writable and self.editor.document().isUndoAvailable())
        self.actions["redo"].setEnabled(writable and self.editor.document().isRedoAvailable())
        for name in ("cut", "paste"):
            self.actions[name].setEnabled(writable)

    def _update_cursor(self):
        """Presenta línea y columna desde uno sin reinterpretar las teclas del usuario."""
        cursor = self.editor.textCursor()
        self.cursor_label.setText(f"Lín. {cursor.blockNumber() + 1}, col. {cursor.positionInBlock() + 1}")

    def _message(self, text):
        """Publica una operación real en la barra de estado y en la consola de lectura."""
        self.message_label.setText(text)
        self.message_label.setToolTip(text)
        self.output_panel.append_message(text)

    def _show_error(self, text):
        """Explica un error de archivos sin descartar el buffer que estaba abierto."""
        self._message("Error: " + text)
        QMessageBox.warning(self, "Piper IDE", text)

    def open_search(self):
        """Muestra la búsqueda y reutiliza una selección de una sola línea como consulta."""
        selected = self.editor.textCursor().selectedText()
        if selected and "\u2029" not in selected and len(selected) < 256:
            self.search_bar.input.setText(selected)
        self.search_bar.show()
        self.search_bar.input.setFocus()
        self.search_bar.input.selectAll()
        self._refresh_search()

    def close_search(self):
        """Oculta la barra y elimina marcas de búsqueda sin borrar la consulta."""
        self.search_timer.stop()
        self.search_bar.hide()
        self.editor.highlight_search("", self.editor.search_flags())
        self.editor.setFocus()

    def _schedule_search(self):
        """Agrupa cambios rápidos del texto/consulta para no recorrer el buffer por tecla."""
        if hasattr(self, "search_timer") and not self.search_bar.isHidden():
            self.search_timer.start()

    def _refresh_search(self):
        """Actualiza coincidencias y contador sobre el documento actualmente visible."""
        flags = self.editor.search_flags(self.search_bar.case_button.isChecked(), self.search_bar.words_button.isChecked())
        count, more = self.editor.highlight_search(self.search_bar.input.text(), flags)
        self.search_bar.result.setText(f"{count}{'+' if more else ''} resultados" if self.search_bar.input.text() else "")

    def find_next(self, backwards=False):
        """Avanza o retrocede una coincidencia literal con vuelta al inicio/final."""
        if self.search_bar.isHidden():
            self.open_search()
        self._refresh_search()
        flags = self.editor.search_flags(self.search_bar.case_button.isChecked(), self.search_bar.words_button.isChecked())
        return self.editor.find_text(self.search_bar.input.text(), flags, backwards)

    def go_to_line(self):
        """Pregunta una línea válida y navega sin modificar el archivo."""
        line, accepted = QInputDialog.getInt(self, "Ir a línea", "Línea:",
            self.editor.textCursor().blockNumber() + 1, 1, self.editor.blockCount(), 1)
        if accepted:
            self.editor.go_to_line(line)
            self.editor.setFocus()

    def show_output_page(self, index: int):
        """Restaura el panel antes de mostrar Problemas, Consola o Clones."""
        self.panel_action.setChecked(True)
        self.output_panel.setCurrentIndex(index)

    def set_theme(self, theme_id: str):
        """Aplica paleta, QSS y resaltadores sin reemplazar ninguno de los buffers."""
        colors = load_theme(theme_id)
        self.theme_id = theme_id
        app = QApplication.instance()
        app.setPalette(palette_for(colors))
        app.setStyleSheet(stylesheet(colors))
        self.setFont(monospace_font(12))
        self.documents.apply_theme(colors)
        self.editor.apply_theme(colors)
        self.explorer.apply_theme(colors)
        self.output_panel.apply_theme(colors)
        self.appearance.apply_theme(colors)
        for button, icon, role in ((self.appearance_button, "settings", "muted"),
                                   (self.save_button, "save", "muted"), (self.compile_button, "play", "accent")):
            button.setIcon(line_icon(icon, colors[role]))
        self.appearance.sync_state(self.theme_id, self.code_font_size)

    def set_code_font_size(self, size: int):
        """Cambia la fuente de todos los documentos, conservando sus cambios e historial."""
        self.code_font_size = max(12, min(19, size))
        self.documents.set_font_size(self.code_font_size)
        self.appearance.sync_state(self.theme_id, self.code_font_size)

    def resizeEvent(self, event):
        """Libera espacio en la fila superior cuando se reduce el ancho de ventana."""
        super().resizeEvent(event)
        self.folder_label.setVisible(self.width() >= 1280)

    def closeEvent(self, event):
        """Permite cancelar el cierre si queda algún documento con cambios pendientes."""
        if self.documents.can_close_window():
            event.accept()
        else:
            event.ignore()
