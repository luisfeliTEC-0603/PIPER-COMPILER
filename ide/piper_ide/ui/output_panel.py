"""Presentación de problemas, salida de herramientas y coincidencias de código.

Los tres contenidos son fijos en la etapa visual. No se analiza el texto ni se
inician procesos; demo_data permite sustituir después estos ejemplos por datos
de los servicios sin cambiar el resto de la ventana.
"""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QPlainTextEdit, QTabWidget, QToolButton,
    QVBoxLayout, QWidget,
)

from .demo_data import CLONE_LEFT, CLONE_RIGHT, CONSOLE_SAMPLE
from .icons import line_icon
from .theme import monospace_font


def _label(text, role=None):
    """Crea texto seleccionable y asigna opcionalmente un rol de color de QSS."""
    label = QLabel(text)
    label.setWordWrap(True)
    label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    if role:
        label.setProperty("role", role)
    return label


class OutputPanel(QTabWidget):
    """Tres pestañas de herramientas; todos sus resultados son de solo lectura.

    El cierre emite closeRequested para que MainWindow cambie la acción de Ver.
    La consola es un visor de salida, sin intérprete de comandos ni terminal.
    """

    closeRequested = Signal()

    def __init__(self, parent=None):
        """Monta las pestañas con ejemplos y el cierre común en la esquina."""
        super().__init__(parent)
        self.setAccessibleName("Panel inferior de ejemplo")
        self.setMinimumHeight(185)
        self.setDocumentMode(True)
        self.tabBar().setDrawBase(False)
        corner = QWidget()
        corner_layout = QHBoxLayout(corner)
        corner_layout.setContentsMargins(8, 0, 8, 0)
        badge = QLabel("VISTA DE EJEMPLO")
        badge.setObjectName("sampleBadge")
        corner_layout.addWidget(badge)
        self.close_button = QToolButton()
        self.close_button.setAccessibleName("Cerrar panel inferior")
        self.close_button.setToolTip("Cerrar panel inferior · se puede mostrar desde Ver")
        self.close_button.clicked.connect(self.closeRequested.emit)
        corner_layout.addWidget(self.close_button)
        self.setCornerWidget(corner)

        # Diagnóstico y corrección representativos: no se recalculan ni se aplican
        # al editar el ejemplo de Piper de la ventana principal.
        problems, layout = self._page()
        layout.addWidget(_label("⌄  P factorial.rwt    tests / codegen", "muted"))
        row = QHBoxLayout()
        row.addWidget(_label("⊗  Falta el terminador . al final de la sentencia.", "error"), 1)
        position = _label("Sintaxis · lín. 10, col. 22", "muted")
        position.setWordWrap(False)
        row.addWidget(position)
        layout.addLayout(row)
        help_row = QHBoxLayout()
        self.suggestion_icon = QLabel()
        self.suggestion_icon.setAccessibleName("Sugerencia · Pokéball de contorno")
        help_row.addWidget(self.suggestion_icon)
        help_row.addWidget(_label("Corrección propuesta: i store i + 1.", "muted"), 1)
        layout.addLayout(help_row)
        layout.addStretch()
        self.addTab(problems, "Problemas  1")

        # QPlainTextEdit en solo lectura conserva selección, copia y scroll,
        # pero no ofrece una entrada para comandos.
        console_page, layout = self._page()
        layout.addWidget(_label("Salida y mensajes de depuración                         Solo lectura", "muted"))
        self.console = QPlainTextEdit()
        self.console.setObjectName("console")
        self.console.setAccessibleName("Consola de salida y depuración, solo lectura")
        self.console.setReadOnly(True)
        self.console.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.console.setFont(monospace_font(12))
        self.console.setPlainText(CONSOLE_SAMPLE)
        layout.addWidget(self.console, 1)
        self.addTab(console_page, "Consola")

        # La comparación solo ilustra la futura revisión automática de Tipo II.
        # No existe un botón de búsqueda ni se ejecuta un detector en esta etapa.
        clones_page, layout = self._page()
        row = QHBoxLayout()
        row.addWidget(_label("1 coincidencia entre archivos", "accent"), 1)
        automatic = _label("Automático · ejemplo    Tipo II", "muted")
        automatic.setWordWrap(False)
        row.addWidget(automatic)
        layout.addLayout(row)
        comparison = QHBoxLayout()
        for filename, text in (("factorial.rwt", CLONE_LEFT), ("factorial_copia.rwt", CLONE_RIGHT)):
            column = QVBoxLayout()
            column.addWidget(_label(f"{filename}  ·  líneas 7–11", "muted"))
            code = QPlainTextEdit(text)
            code.setReadOnly(True)
            code.setFont(monospace_font(12))
            code.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
            column.addWidget(code, 1)
            comparison.addLayout(column, 1)
        layout.addLayout(comparison, 1)
        self.addTab(clones_page, "Clones  1")

    @staticmethod
    def _page():
        """Devuelve una página y su layout con márgenes comunes para cada pestaña."""
        page = QWidget()
        page.setObjectName("outputPage")
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 14, 20, 12)
        layout.setSpacing(12)
        return page, layout

    def apply_theme(self, colors):
        """Actualiza el cierre y la Pokéball, rasterizados con el color del tema."""
        self.close_button.setIcon(line_icon("close", colors["muted"]))
        self.suggestion_icon.setPixmap(line_icon("pokeball", colors["accent"]).pixmap(17, 17))
