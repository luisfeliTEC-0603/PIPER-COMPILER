"""Explorador real con documentos abiertos y carga de carpetas bajo demanda.

Refrescar solo reconstruye la lista de rutas; nunca recarga los buffers. No se
recorre todo el proyecto al abrirlo: cada carpeta se enumera al expandirla.
build tiene su propio árbol para que Generados siga visible aunque Git lo ignore.
"""

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QPushButton,
    QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

from ..models.project import Project
from .icons import line_icon


_PATH = Qt.ItemDataRole.UserRole
_LOADED = Qt.ItemDataRole.UserRole + 1
_HIDDEN_DIRECTORIES = {".git", ".venv", ".python", "__pycache__", "node_modules", ".pytest_cache", ".mypy_cache"}
_HIDDEN_FILES = {".DS_Store"}


class Explorer(QWidget):
    """Emite rutas/identidades; la ventana decide cómo abrir o cerrar documentos."""

    fileRequested = Signal(object)
    documentRequested = Signal(str)
    newRequested = Signal()
    folderRequested = Signal()
    refreshed = Signal()
    errorOccurred = Signal(str)

    def __init__(self, parent=None):
        """Construye los controles y los dos árboles, inicialmente sin carpeta."""
        super().__init__(parent)
        self.setObjectName("explorer")
        self.setMinimumWidth(220)
        self.setMaximumWidth(480)
        self.project = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(10)
        heading = QHBoxLayout()
        heading.addWidget(self._heading("EXPLORADOR"), 1)
        self.new_button = QToolButton()
        self.new_button.setAccessibleName("Nuevo archivo")
        self.new_button.setToolTip("Nuevo archivo · Ctrl+N")
        self.new_button.clicked.connect(self.newRequested.emit)
        heading.addWidget(self.new_button)
        self.refresh_button = QToolButton()
        self.refresh_button.setAccessibleName("Refrescar explorador")
        self.refresh_button.setToolTip("Refrescar rutas sin recargar los documentos abiertos")
        self.refresh_button.clicked.connect(self.refresh)
        heading.addWidget(self.refresh_button)
        layout.addLayout(heading)
        layout.addWidget(self._heading("ABIERTOS"))
        self.open_list = QListWidget()
        self.open_list.setAccessibleName("Documentos abiertos")
        self.open_list.setMinimumHeight(65)
        self.open_list.setMaximumHeight(160)
        self.open_list.currentItemChanged.connect(self._choose_document)
        layout.addWidget(self.open_list)
        self.folder_button = QPushButton("Abrir carpeta…")
        self.folder_button.clicked.connect(self.folderRequested.emit)
        layout.addWidget(self.folder_button)
        self.root_label = self._heading("SIN CARPETA")
        layout.addWidget(self.root_label)
        self.source_tree = self._tree("Archivos del proyecto")
        layout.addWidget(self.source_tree, 2)
        layout.addWidget(self._heading("GENERADOS"))
        self.generated_path = QLabel("build")
        self.generated_path.setProperty("role", "muted")
        layout.addWidget(self.generated_path)
        self.generated_tree = self._tree("Archivos generados en build")
        layout.addWidget(self.generated_tree, 1)
        self.generated_empty = QLabel("Sin carpeta build. Las salidas aparecerán aquí al refrescar.")
        self.generated_empty.setWordWrap(True)
        self.generated_empty.setProperty("role", "muted")
        layout.addWidget(self.generated_empty)
        self.refresh()

    @staticmethod
    def _heading(text):
        """Crea un título que toma el color semántico del tema."""
        label = QLabel(text)
        label.setProperty("role", "eyebrow")
        return label

    def _tree(self, name):
        """Crea un árbol sin columnas extra, renombrado ni operaciones destructivas."""
        tree = QTreeWidget()
        tree.setHeaderHidden(True)
        tree.setAccessibleName(name)
        tree.setIndentation(14)
        tree.setMinimumHeight(75)
        tree.itemExpanded.connect(self._populate)
        tree.itemClicked.connect(self._choose_file)
        tree.itemActivated.connect(self._choose_file)
        return tree

    def set_project(self, project: Project):
        """Cambia la carpeta visible sin cerrar documentos de la carpeta anterior."""
        self.project = project
        self.root_label.setText(project.root.name)
        self.root_label.setToolTip(str(project.root))
        self.generated_path.setText("build / " + project.root.name)
        self.refresh()

    def _entries(self, directory: Path):
        """Enumera solo el directorio pedido; los errores se notifican sin cerrarlo."""
        try:
            entries = [entry for entry in directory.iterdir()
                       if entry.name not in _HIDDEN_FILES
                       and not (entry.is_dir() and entry.name in _HIDDEN_DIRECTORIES)]
            if self.project and directory == self.project.root:
                entries = [entry for entry in entries if entry != self.project.generated_root]
            return sorted(entries, key=lambda entry: (not entry.is_dir(), entry.name.casefold()))
        except OSError as error:
            self.errorOccurred.emit(f"No se pudo listar {directory}: {error}")
            return []

    def _add_entries(self, parent, directory: Path):
        """Inserta archivos y carpetas con un hijo provisional para expansión diferida."""
        for path in self._entries(directory):
            item = QTreeWidgetItem([path.name])
            item.setData(0, _PATH, str(path))
            item.setToolTip(0, str(path))
            if isinstance(parent, QTreeWidget):
                parent.addTopLevelItem(item)
            else:
                parent.addChild(item)
            if path.is_dir():
                QTreeWidgetItem(item, ["…"])

    def _populate(self, item):
        """Sustituye el hijo provisional por la lista real, una vez por refresco."""
        if item.data(0, _LOADED):
            return
        value = item.data(0, _PATH)
        if not value:
            return
        path = Path(value)
        if path.is_dir():
            item.takeChildren()
            self._add_entries(item, path)
            item.setData(0, _LOADED, True)

    def _choose_file(self, item, column=0):
        """Abre con clic o Enter solo archivos; expandir carpetas no crea buffers."""
        value = item.data(0, _PATH)
        if value and not Path(value).is_dir():
            self.fileRequested.emit(Path(value))

    def _choose_document(self, item, previous):
        """Traduce la selección lateral a la identidad estable del buffer."""
        if item:
            self.documentRequested.emit(item.data(_PATH))

    def set_documents(self, sessions, active_id: str):
        """Actualiza nombres y marcas de cambios sin emitir navegación involuntaria."""
        scroll = self.open_list.verticalScrollBar().value()
        self.open_list.blockSignals(True)
        self.open_list.clear()
        for session in sessions:
            info = session.info
            prefix = "● " if session.buffer.isModified() else "  "
            item = QListWidgetItem(prefix + info.name + ("  [lectura]" if info.read_only else ""))
            item.setData(_PATH, info.id)
            item.setToolTip(str(info.path) if info.path else "Nuevo archivo sin guardar")
            self.open_list.addItem(item)
            if info.id == active_id:
                self.open_list.setCurrentItem(item)
        self.open_list.verticalScrollBar().setValue(scroll)
        self.open_list.setFixedHeight(min(160, max(38, len(sessions) * 30 + 4)))
        self.open_list.blockSignals(False)

    def _expanded_paths(self, tree):
        """Recoge únicamente nodos ya cargados; no fuerza un recorrido del disco."""
        paths = []
        pending = [tree.topLevelItem(index) for index in range(tree.topLevelItemCount())]
        while pending:
            item = pending.pop()
            if item.isExpanded():
                paths.append(item.data(0, _PATH))
            pending.extend(item.child(index) for index in range(item.childCount()))
        return set(paths)

    def _restore_expanded(self, parent, paths):
        """Restaura carpetas expandidas después de volver a enumerar el árbol."""
        count = parent.topLevelItemCount() if isinstance(parent, QTreeWidget) else parent.childCount()
        for index in range(count):
            item = parent.topLevelItem(index) if isinstance(parent, QTreeWidget) else parent.child(index)
            if item.data(0, _PATH) in paths:
                item.setExpanded(True)
                self._restore_expanded(item, paths)

    def refresh(self):
        """Refresca ambos árboles preservando expansión y todos los buffers abiertos."""
        for tree, directory in (
            (self.source_tree, self.project.root if self.project else None),
            (self.generated_tree, self.project.generated_root if self.project else None),
        ):
            expanded = self._expanded_paths(tree)
            scroll = tree.verticalScrollBar().value()
            tree.clear()
            if directory and directory.is_dir():
                self._add_entries(tree, directory)
                self._restore_expanded(tree, expanded)
            tree.verticalScrollBar().setValue(scroll)
        has_build = bool(self.project and self.project.generated_root.is_dir())
        self.generated_tree.setVisible(has_build)
        self.generated_empty.setVisible(not has_build)
        self.refresh_button.setEnabled(self.project is not None)
        self.refreshed.emit()

    def apply_theme(self, colors):
        """Recrea los iconos del encabezado con el color del tema."""
        self.new_button.setIcon(line_icon("plus", colors["muted"]))
        self.refresh_button.setIcon(line_icon("refresh", colors["muted"]))
