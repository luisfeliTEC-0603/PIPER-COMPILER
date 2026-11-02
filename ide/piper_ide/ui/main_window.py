"""Composición de la ventana y conexiones entre sus componentes visuales.

MainWindow coordina los menús, la visibilidad de paneles y la apariencia.
Los componentes hijos se encargan de dibujar su contenido. En esta etapa el
editor trabaja en memoria y no se importan servicios de archivos o compilación.
"""

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QIcon, QPixmap
from PySide6.QtWidgets import (
    QApplication, QComboBox, QFrame, QHBoxLayout, QLabel, QMainWindow,
    QMenu, QPushButton, QSplitter, QToolButton, QVBoxLayout, QWidget,
)

from ..editor.code_editor import CodeEditor
from .appearance_panel import AppearancePanel
from .demo_data import PIPER_SAMPLE
from .explorer import Explorer
from .icons import line_icon
from .output_panel import OutputPanel
from .theme import load_theme, monospace_font, palette_for, resource_path, stylesheet


class MainWindow(QMainWindow):
    """Ventana con barra superior, explorador, editor y herramientas inferiores.

    Los splitters permiten repartir el espacio con el mouse. El estado de tema
    y tamaño pertenece a esta ventana y dura solo la sesión. Las QAction de Ver
    mantienen sincronizados el menú y los botones que cierran cada panel.
    """

    def __init__(self):
        """Construye los hijos antes de conectar acciones y aplicar el tema."""
        super().__init__()
        self.setWindowTitle("Piper IDE · etapa visual")
        self.setWindowIcon(QIcon(resource_path("images/rowlet.png")))
        self.resize(1440, 940)
        self.setMinimumSize(1120, 680)
        self.theme_id = "rowlet-oscuro"
        self.code_font_size = 14

        # La barra superior y la de estado quedan fuera de los splitters:
        # redimensionar un panel no mueve los controles generales del IDE.
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
        self.editor = CodeEditor()
        self.editor.setPlainText(PIPER_SAMPLE)
        self.editor.document().setModified(False)
        # El documento Qt conserva el historial de Deshacer. Su señal de cambios
        # también permite quitar «sin guardar» si se deshace hasta el estado inicial.
        self.editor.document().modificationChanged.connect(self._set_modified)
        self.editor.cursorPositionChanged.connect(self._update_cursor)
        editor_layout.addWidget(self.editor, 1)
        self.editor_splitter.addWidget(editor_area)
        self.output_panel = OutputPanel()
        # Se cambia la acción del menú, que a su vez oculta el panel. La lambda
        # se ejecutará al pulsar cerrar, cuando _menus ya haya creado esa acción.
        self.output_panel.closeRequested.connect(lambda: self.panel_action.setChecked(False))
        self.editor_splitter.addWidget(self.output_panel)
        self.editor_splitter.setStretchFactor(0, 1)
        self.editor_splitter.setSizes([620, 215])
        self.workspace_splitter.addWidget(self.editor_splitter)
        self.workspace_splitter.setStretchFactor(1, 1)
        self.workspace_splitter.setSizes([240, 1200])
        workspace.addWidget(self.workspace_splitter, 1)

        self.appearance = AppearancePanel()
        self.appearance.hide()
        self.appearance.themeChanged.connect(self.set_theme)
        self.appearance.fontSizeChanged.connect(self.set_code_font_size)
        self.appearance.closeRequested.connect(lambda: self.appearance_button.setChecked(False))
        workspace.addWidget(self.appearance)
        layout.addLayout(workspace, 1)
        layout.addWidget(self._statusbar())
        self._menus()
        self.appearance_button.toggled.connect(self.appearance.setVisible)
        self.set_theme(self.theme_id)
        self._update_cursor()
        self.editor.setFocus()

    def _topbar(self):
        """Construye la fila de marca, menús, apariencia y controles de compilación.

        Los valores del selector son identificadores del CLI que se conectará
        después. Guardar y Compilar permanecen deshabilitados mientras no exista
        la integración; seleccionar una salida todavía no ejecuta un proceso.
        """
        bar = QFrame()
        bar.setObjectName("topbar")
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(16, 10, 16, 10)
        layout.setSpacing(8)
        image = QLabel()
        image.setPixmap(QPixmap(resource_path("images/rowlet.png")).scaled(
            34, 38, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
        layout.addWidget(image)
        brand = QLabel("Piper IDE")
        brand.setObjectName("brand")
        layout.addWidget(brand)
        layout.addSpacing(8)
        self.menu_buttons = {}
        for name in ("Archivo", "Editar", "Ver"):
            button = QPushButton(name)
            layout.addWidget(button)
            self.menu_buttons[name] = button
        layout.addStretch()
        self.folder_label = QLabel("Carpeta: PIPER-COMPILER")
        self.folder_label.setProperty("role", "muted")
        layout.addWidget(self.folder_label)
        layout.addStretch()
        self.appearance_button = QToolButton()
        self.appearance_button.setCheckable(True)
        self.appearance_button.setAccessibleName("Apariencia")
        self.appearance_button.setToolTip("Apariencia")
        layout.addWidget(self.appearance_button)
        self.save_button = QPushButton("Guardar  Ctrl+S")
        self.save_button.setEnabled(False)
        self.save_button.setToolTip("Guardar en disco · se conectará en la etapa 2")
        layout.addWidget(self.save_button)
        target_label = QLabel("Salida")
        target_label.setProperty("role", "muted")
        layout.addWidget(target_label)
        self.compile_target = QComboBox()
        self.compile_target.setAccessibleName("Salida de compilación")
        for title, command in (
            ("Binario (.bin)", "bin"), ("ASM (.asm)", "asm"),
            ("Bundles (.bundles)", "sch"), ("Memoria (.mem)", "mem"),
            ("AST y símbolos", "ins"), ("Todo", "all"),
        ):
            self.compile_target.addItem(title, command)
        self.compile_target.setToolTip("Selección visual · la compilación se conectará en la etapa 4")
        layout.addWidget(self.compile_target)
        self.compile_button = QPushButton("Compilar  F5")
        self.compile_button.setObjectName("compileButton")
        self.compile_button.setEnabled(False)
        self.compile_button.setToolTip("Ejecutar pipoc · se conectará en la etapa 4")
        layout.addWidget(self.compile_button)
        return bar

    def _editor_heading(self):
        """Crea la cabecera del archivo de ejemplo y su indicador de cambios.

        Las rutas son etiquetas de demostración, no documentos abiertos en disco.
        La cabecera sustituye las pestañas de archivos en el diseño aprobado.
        """
        header = QFrame()
        header.setObjectName("editorHeading")
        layout = QHBoxLayout(header)
        layout.setContentsMargins(24, 12, 24, 12)
        column = QVBoxLayout()
        column.setSpacing(4)
        label = QLabel("ARCHIVO ACTIVO · EJEMPLO")
        label.setProperty("role", "eyebrow")
        column.addWidget(label)
        filename = QLabel("factorial.rwt")
        filename.setObjectName("activeFilename")
        column.addWidget(filename)
        path = QLabel("tests/codegen/factorial.rwt")
        path.setProperty("role", "muted")
        column.addWidget(path)
        layout.addLayout(column)
        self.modified_label = QLabel("sin guardar")
        self.modified_label.setProperty("role", "error")
        self.modified_label.hide()
        layout.addWidget(self.modified_label)
        layout.addStretch()
        language = QLabel("PIPER / .rwt")
        language.setProperty("role", "eyebrow")
        layout.addWidget(language)
        return header

    def _statusbar(self):
        """Presenta el alcance de la etapa y reserva la etiqueta del cursor.

        Solo la posición del cursor cambia al editar. UTF-8 y el tipo Piper
        describen el ejemplo visual, no una lectura real de un archivo.
        """
        bar = QFrame()
        bar.setObjectName("statusbar")
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(16, 5, 16, 5)
        layout.setSpacing(18)
        status = QLabel("●  Etapa 1 · datos de ejemplo")
        status.setProperty("role", "accent")
        layout.addWidget(status)
        note = QLabel("Cambios del editor solo en memoria")
        note.setProperty("role", "muted")
        layout.addWidget(note)
        layout.addStretch()
        self.cursor_label = QLabel()
        layout.addWidget(self.cursor_label)
        for text in ("Espacios: 4", "UTF-8", "Piper"):
            label = QLabel(text)
            label.setProperty("role", "muted")
            layout.addWidget(label)
        return bar

    def _menus(self):
        """Conecta edición en memoria y visibilidad; deja pendiente el acceso a disco.

        Ver usa acciones comprobables como fuente del estado de cada panel.
        Al elegir una pestaña desde ese menú también se restaura el panel oculto.
        """
        file_menu = QMenu(self)
        for title in ("Nuevo archivo · etapa 2", "Abrir carpeta… · etapa 2", "Guardar · etapa 2"):
            action = file_menu.addAction(title)
            action.setEnabled(False)
        file_menu.addSeparator()
        file_menu.addAction("Salir", self.close)
        self.menu_buttons["Archivo"].setMenu(file_menu)

        edit_menu = QMenu(self)
        # Las acciones actúan únicamente sobre el buffer de demostración.
        for title, shortcut, slot in (
            ("Deshacer", "Ctrl+Z", self.editor.undo),
            ("Rehacer", "Ctrl+Shift+Z", self.editor.redo),
            ("Seleccionar todo", "Ctrl+A", self.editor.selectAll),
        ):
            action = edit_menu.addAction(title, slot)
            action.setShortcut(shortcut)
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
            view_menu.addAction(title, lambda checked=False, value=index: self.show_output_page(value))
        self.menu_buttons["Ver"].setMenu(view_menu)

    def show_output_page(self, index: int):
        """Muestra el panel y selecciona Problemas (0), Consola (1) o Clones (2)."""
        self.panel_action.setChecked(True)
        self.output_panel.setCurrentIndex(index)

    def _set_modified(self, modified: bool):
        """Refleja en la cabecera el estado de modificación del documento Qt."""
        self.modified_label.setVisible(modified)

    def _update_cursor(self):
        """Convierte las posiciones de Qt, que comienzan en cero, a línea/columna.

        Durante la construcción el editor puede emitir la señal antes de que
        exista la barra de estado; en ese caso se espera a la actualización final.
        """
        if hasattr(self, "cursor_label"):
            cursor = self.editor.textCursor()
            self.cursor_label.setText(f"Lín. {cursor.blockNumber() + 1}, col. {cursor.positionInBlock() + 1}")

    def set_theme(self, theme_id: str):
        """Aplica una paleta por su identificador sin reemplazar el documento.

        Combina QPalette para controles nativos con QSS para las zonas propias.
        Los iconos y el dibujo manual del editor deben actualizarse aparte.
        No toca el contenido, la selección ni el historial de Deshacer.
        """
        colors = load_theme(theme_id)
        self.theme_id = theme_id
        app = QApplication.instance()
        app.setPalette(palette_for(colors))
        app.setStyleSheet(stylesheet(colors))
        self.setFont(monospace_font(12))
        self.editor.apply_theme(colors)
        self.explorer.apply_theme(colors)
        self.output_panel.apply_theme(colors)
        self.appearance.apply_theme(colors)
        for button, icon, key in (
            (self.appearance_button, "settings", "muted"),
            (self.save_button, "save", "muted"),
            (self.compile_button, "play", "accent"),
        ):
            button.setIcon(line_icon(icon, colors[key]))
        self.appearance.sync_state(self.theme_id, self.code_font_size)

    def set_code_font_size(self, size: int):
        """Ajusta solo el editor entre 12 y 19 px y sincroniza el control visual."""
        self.code_font_size = max(12, min(19, size))
        self.editor.set_code_font_size(self.code_font_size)
        self.appearance.sync_state(self.theme_id, self.code_font_size)

    def resizeEvent(self, event):
        """Libera espacio en la barra superior al reducir el ancho de la ventana."""
        super().resizeEvent(event)
        # En una ventana estrecha la carpeta sigue identificada en el explorador.
        self.folder_label.setVisible(self.width() >= 1280)
