"""Lista lateral de fuentes y grupo de salidas, con datos fijos de demostración.

Todavía no consulta directorios, abre documentos ni refresca archivos. La fila
activa y los nombres se muestran para revisar la distribución de la ventana.
"""

from PySide6.QtCore import QSize
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QToolButton, QVBoxLayout, QWidget

from .icons import line_icon


class Explorer(QWidget):
    """Presentación provisional del explorador que se conectará en la etapa 2.

    Conserva visibles Nuevo y Refrescar, pero deshabilitados. Los nombres largos
    se acortan en pantalla; el tooltip mantiene la ruta completa de ejemplo.
    """

    def __init__(self, parent=None):
        """Crea las secciones Fuentes y Generados, sin efectuar operaciones de disco."""
        super().__init__(parent)
        self.setObjectName("explorer")
        self.setMinimumWidth(210)
        self.setMaximumWidth(420)
        self.setAccessibleName("Explorador de ejemplo")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)
        heading = QHBoxLayout()
        title = QLabel("EXPLORADOR")
        title.setProperty("role", "eyebrow")
        heading.addWidget(title)
        heading.addStretch()
        self.new_button = QToolButton()
        self.refresh_button = QToolButton()
        for button, name in ((self.new_button, "Nuevo archivo"), (self.refresh_button, "Refrescar explorador")):
            button.setAccessibleName(name)
            button.setToolTip(f"{name} · disponible al conectar los archivos en la etapa 2")
            button.setIconSize(QSize(15, 15))
            button.setEnabled(False)
            heading.addWidget(button)
        layout.addLayout(heading)
        layout.addSpacing(12)
        root = QLabel("PIPER-COMPILER   ·   4")
        root.setProperty("role", "eyebrow")
        layout.addWidget(root)
        path = QLabel("tests / codegen")
        path.setProperty("role", "muted")
        layout.addWidget(path)
        self._divider(layout)
        for index, name in enumerate(("factorial.rwt", "busqueda_matriz.rwt", "crypto_feistel_pipeline.rwt", "factorial_copia.rwt")):
            row = QLabel("P  " + (name if len(name) < 23 else name[:21] + "…"))
            row.setObjectName("fileRow")
            row.setProperty("active", index == 0)
            row.setToolTip(f"tests/codegen/{name}\nLista de ejemplo · navegación pendiente de la etapa 2")
            layout.addWidget(row)
        layout.addSpacing(18)
        self._divider(layout)
        generated = QLabel("GENERADOS   ·   0")
        generated.setProperty("role", "eyebrow")
        layout.addWidget(generated)
        path = QLabel("build / ide")
        path.setProperty("role", "muted")
        layout.addWidget(path)
        note = QLabel("Al conectar la compilación,\nlas salidas aparecerán aquí.")
        note.setProperty("role", "muted")
        note.setWordWrap(True)
        layout.addSpacing(12)
        layout.addWidget(note)
        layout.addStretch()
        self._divider(layout)
        foot = QLabel("●  Archivos de ejemplo")
        foot.setProperty("role", "muted")
        layout.addWidget(foot)

    @staticmethod
    def _divider(layout):
        """Añade un separador al layout recibido; QSS define su color."""
        line = QFrame()
        line.setObjectName("divider")
        line.setFixedHeight(1)
        layout.addWidget(line)

    def apply_theme(self, colors):
        """Recrea los iconos para que su trazo coincida con la nueva paleta."""
        self.new_button.setIcon(line_icon("plus", colors["muted"]))
        self.refresh_button.setIcon(line_icon("refresh", colors["muted"]))
