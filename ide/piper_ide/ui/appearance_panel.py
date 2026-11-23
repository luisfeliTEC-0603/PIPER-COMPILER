"""Controles de apariencia de la sesión, sin persistencia de preferencias.

El panel emite la selección del usuario; MainWindow aplica el cambio y devuelve
el estado actualizado mediante sync_state. Así el panel no conoce el editor.
"""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QButtonGroup, QFrame, QHBoxLayout, QLabel, QPushButton,
    QSlider, QToolButton, QVBoxLayout,
)

from .icons import line_icon


class AppearancePanel(QFrame):
    """Selector de tema y tamaño con señales para la ventana que lo contiene.

    themeChanged emite el identificador JSON; fontSizeChanged, el tamaño en px.
    closeRequested permite cerrar el panel manteniendo su botón sincronizado.
    """

    themeChanged = Signal(str)
    fontSizeChanged = Signal(int)
    closeRequested = Signal()

    def __init__(self, parent=None):
        """Construye opciones de tema mutuamente excluyentes y un rango de 12–19 px."""
        super().__init__(parent)
        self.setObjectName("appearancePanel")
        self.setFixedWidth(306)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(22, 22, 22, 22)
        layout.setSpacing(18)
        header = QHBoxLayout()
        title = QLabel("Apariencia")
        title.setStyleSheet("font-size: 19px; font-weight: bold;")
        header.addWidget(title, 1)
        self.close_button = QToolButton()
        self.close_button.setAccessibleName("Cerrar apariencia")
        self.close_button.clicked.connect(self.closeRequested.emit)
        header.addWidget(self.close_button)
        layout.addLayout(header)
        intro = QLabel("Paleta Rowlet oscura como base.\nTambién disponible en modo claro.")
        intro.setProperty("role", "muted")
        intro.setWordWrap(True)
        layout.addWidget(intro)
        layout.addWidget(QLabel("TEMA DE LA INTERFAZ"))
        self.theme_buttons = {}
        self.theme_group = QButtonGroup(self)
        for theme_id, title in (("rowlet-oscuro", "Modo oscuro"), ("rowlet-shiny", "Modo claro")):
            button = QPushButton(title)
            button.setObjectName("themeChoice")
            button.setCheckable(True)
            # El valor por defecto captura el tema de cada iteración; sin esa
            # captura ambos botones terminarían emitiendo el último identificador.
            button.clicked.connect(lambda checked, value=theme_id: self.themeChanged.emit(value))
            self.theme_group.addButton(button)
            self.theme_buttons[theme_id] = button
            layout.addWidget(button)
        font_row = QHBoxLayout()
        font_row.addWidget(QLabel("Tamaño del código · Monaco"), 1)
        self.font_value = QLabel("14 px")
        font_row.addWidget(self.font_value)
        layout.addLayout(font_row)
        self.font_slider = QSlider(Qt.Orientation.Horizontal)
        self.font_slider.setRange(12, 19)
        self.font_slider.setValue(14)
        self.font_slider.setAccessibleName("Tamaño del código en píxeles")
        self.font_slider.valueChanged.connect(self.fontSizeChanged.emit)
        layout.addWidget(self.font_slider)
        reset = QPushButton("Restablecer apariencia")
        reset.setObjectName("resetAppearance")
        reset.clicked.connect(self._reset)
        layout.addWidget(reset)
        note = QLabel("Las preferencias se aplican a esta sesión. Las guías de indentación permanecen visibles.")
        note.setWordWrap(True)
        note.setProperty("role", "muted")
        layout.addWidget(note)
        layout.addStretch()

    def _reset(self):
        """Solicita los valores iniciales sin escribir preferencias en disco."""
        self.themeChanged.emit("rowlet-oscuro")
        self.fontSizeChanged.emit(14)

    def sync_state(self, theme_id: str, size: int):
        """Refleja el estado aplicado sin emitir otra petición de cambio.

        Bloquear temporalmente la señal del slider evita que actualizar su valor
        desde la ventana vuelva a llamar a set_code_font_size indefinidamente.
        """
        self.theme_buttons[theme_id].setChecked(True)
        self.font_slider.blockSignals(True)
        self.font_slider.setValue(size)
        self.font_slider.blockSignals(False)
        self.font_value.setText(f"{size} px")

    def apply_theme(self, colors):
        """Actualiza el icono de cierre, cuyo color no lo modifica QSS."""
        self.close_button.setIcon(line_icon("close", colors["muted"]))
