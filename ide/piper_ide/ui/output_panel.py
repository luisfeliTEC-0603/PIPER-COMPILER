"""Paneles de diagnósticos, salida real de pipoc y clones."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QPlainTextEdit,
    QTabWidget, QToolButton, QVBoxLayout, QWidget,
)

from .icons import line_icon
from .theme import monospace_font


class OutputPanel(QTabWidget):
    """Visores de solo lectura; el cierre se comunica a la acción Ver de la ventana."""

    closeRequested = Signal()
    diagnosticActivated = Signal(int, int)

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
        self.problems_empty = QLabel("Pipoc no reportó errores. Sus diagnósticos aparecerán aquí.")
        self.problems_empty.setWordWrap(True)
        self.problems_empty.setProperty("role", "muted")
        row.addWidget(self.problems_empty, 1)
        layout.addLayout(row)
        self.problems_list = QListWidget()
        self.problems_list.setAccessibleName("Diagnósticos de pipoc")
        self.problems_list.itemActivated.connect(self._activate_diagnostic)
        layout.addWidget(self.problems_list, 1)
        self.problems_list.hide()
        self.addTab(problems, "Problemas")
        console_page, layout = self._page()
        title = QLabel("Salida de Pipoc y mensajes del IDE · solo lectura")
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

    def begin_compilation(self, command: str):
        """Abre la terminal, separa la ejecución y publica el comando exacto."""
        self.setCurrentIndex(1)
        self.console.appendPlainText("\n$ " + command)
        self.clear_diagnostics()

    def append_process_output(self, _channel: str, text: str):
        """Inserta chunks del proceso sin añadir saltos que pipoc no emitió."""
        cursor = self.console.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        cursor.insertText(text)
        self.console.setTextCursor(cursor)
        self.console.ensureCursorVisible()

    def clear_diagnostics(self):
        self.problems_list.clear()
        self.problems_list.hide()
        self.problems_empty.show()
        self.setTabText(0, "Problemas")

    def set_diagnostics(self, diagnostics):
        """Reemplaza resultados y conserva línea/columna para navegación."""
        self.problems_list.clear()
        for diagnostic in diagnostics:
            item = QListWidgetItem(
                f"{diagnostic.location_label}  {diagnostic.message}"
            )
            item.setData(Qt.ItemDataRole.UserRole, (diagnostic.line, diagnostic.column))
            self.problems_list.addItem(item)
        has_items = self.problems_list.count() > 0
        self.problems_list.setVisible(has_items)
        self.problems_empty.setVisible(not has_items)
        self.setTabText(
            0,
            f"Problemas ({self.problems_list.count()})" if has_items else "Problemas",
        )
        if has_items:
            self.setCurrentIndex(0)

    def _activate_diagnostic(self, item):
        location = item.data(Qt.ItemDataRole.UserRole)
        if location:
            self.diagnosticActivated.emit(*location)

    def apply_theme(self, colors):
        """Actualiza los iconos cuyo trazo no cambia mediante QSS."""
        self.close_button.setIcon(line_icon("close", colors["muted"]))
        self.suggestion_icon.setPixmap(line_icon("pokeball", colors["accent"]).pixmap(17, 17))
