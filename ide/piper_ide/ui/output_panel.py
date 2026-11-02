"""Paneles inferiores sin resultados simulados sobre archivos reales.

Consola registra las operaciones de archivos. Problemas y Clones esperan las
integraciones posteriores; todavía no hay un analizador ejecutándose detrás.
"""

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPlainTextEdit, QTabWidget, QToolButton, QVBoxLayout, QWidget

from .icons import line_icon
from .theme import monospace_font


class OutputPanel(QTabWidget):
    """Visores de solo lectura; el cierre se comunica a la acción Ver de la ventana."""

    closeRequested = Signal()

    def __init__(self, parent=None):
        """Crea estados vacíos honestos y una consola de mensajes reales del IDE."""
        super().__init__(parent)
        self.setAccessibleName("Herramientas del IDE")
        self.setMinimumHeight(185)
        self.setDocumentMode(True)
        self.tabBar().setDrawBase(False)
        corner = QWidget()
        corner_layout = QHBoxLayout(corner)
        corner_layout.setContentsMargins(8, 0, 8, 0)
        self.close_button = QToolButton()
        self.close_button.setAccessibleName("Cerrar panel inferior")
        self.close_button.setToolTip("Cerrar panel inferior · mostrar desde Ver")
        self.close_button.clicked.connect(self.closeRequested.emit)
        corner_layout.addWidget(self.close_button)
        self.setCornerWidget(corner)
        problems, layout = self._page()
        row = QHBoxLayout()
        self.suggestion_icon = QLabel()
        row.addWidget(self.suggestion_icon)
        text = QLabel("El análisis del compilador aún no está conectado. Sus diagnósticos aparecerán aquí.")
        text.setWordWrap(True)
        text.setProperty("role", "muted")
        row.addWidget(text, 1)
        layout.addLayout(row)
        layout.addStretch()
        self.addTab(problems, "Problemas")
        console_page, layout = self._page()
        title = QLabel("Salida y mensajes del IDE · solo lectura")
        title.setProperty("role", "muted")
        layout.addWidget(title)
        self.console = QPlainTextEdit()
        self.console.setObjectName("console")
        self.console.setAccessibleName("Consola de mensajes, solo lectura")
        self.console.setReadOnly(True)
        self.console.setMaximumBlockCount(2000)
        self.console.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.console.setFont(monospace_font(12))
        self.console.setPlainText("[IDE] Listo para abrir, editar y guardar archivos.")
        layout.addWidget(self.console, 1)
        self.addTab(console_page, "Consola")
        clones, layout = self._page()
        note = QLabel("La detección automática de clones se conectará en una etapa posterior.")
        note.setWordWrap(True)
        note.setProperty("role", "muted")
        layout.addWidget(note)
        layout.addStretch()
        self.addTab(clones, "Clones")

    @staticmethod
    def _page():
        """Devuelve una página y su layout con márgenes comunes."""
        page = QWidget()
        page.setObjectName("outputPage")
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 14, 20, 12)
        layout.setSpacing(12)
        return page, layout

    def append_message(self, message: str):
        """Añade texto literal al registro; no interpreta comandos ni HTML."""
        self.console.appendPlainText("[IDE] " + message)

    def apply_theme(self, colors):
        """Actualiza los iconos cuyo trazo no cambia mediante QSS."""
        self.close_button.setIcon(line_icon("close", colors["muted"]))
        self.suggestion_icon.setPixmap(line_icon("pokeball", colors["accent"]).pixmap(17, 17))
