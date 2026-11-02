"""Barra de búsqueda literal; comunica opciones sin conocer el archivo activo."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QKeySequence, QShortcut
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QLineEdit, QToolButton


class SearchBar(QFrame):
    """Consulta, opciones de coincidencia y navegación con teclado o mouse."""

    queryChanged = Signal()
    findRequested = Signal(bool)
    closeRequested = Signal()

    def __init__(self, parent=None):
        """Construye controles compactos y atajos activos solo dentro de la barra."""
        super().__init__(parent)
        self.setObjectName("searchBar")
        row = QHBoxLayout(self)
        row.setContentsMargins(16, 6, 16, 6)
        self.input = QLineEdit()
        self.input.setPlaceholderText("Buscar en este archivo…")
        self.input.setAccessibleName("Texto para buscar")
        self.input.textChanged.connect(lambda text: self.queryChanged.emit())
        self.input.returnPressed.connect(lambda: self.findRequested.emit(False))
        row.addWidget(self.input, 1)
        self.case_button = self._option("Aa", "Distinguir mayúsculas y minúsculas")
        self.words_button = self._option("Palabra", "Buscar palabras completas")
        row.addWidget(self.case_button)
        row.addWidget(self.words_button)
        self.result = QLabel()
        self.result.setMinimumWidth(96)
        row.addWidget(self.result)
        for text, label, slot in (("↑", "Coincidencia anterior · Shift+Enter", lambda: self.findRequested.emit(True)),
                                  ("↓", "Coincidencia siguiente · Enter", lambda: self.findRequested.emit(False)),
                                  ("×", "Cerrar búsqueda · Esc", self.closeRequested.emit)):
            button = QToolButton()
            button.setText(text)
            button.setToolTip(label)
            button.setAccessibleName(label)
            button.clicked.connect(slot)
            row.addWidget(button)
        for key, slot in (("Escape", self.closeRequested.emit), ("Shift+Return", lambda: self.findRequested.emit(True))):
            shortcut = QShortcut(QKeySequence(key), self)
            shortcut.setContext(Qt.ShortcutContext.WidgetWithChildrenShortcut)
            shortcut.activated.connect(slot)

    def _option(self, text, label):
        """Crea una opción comprobable que solicita actualizar las coincidencias."""
        button = QToolButton()
        button.setText(text)
        button.setCheckable(True)
        button.setToolTip(label)
        button.setAccessibleName(label)
        button.toggled.connect(lambda checked: self.queryChanged.emit())
        return button
