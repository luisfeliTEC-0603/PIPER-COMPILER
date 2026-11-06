"""Panel lateral para explorar los grupos de clones detectados.

Cada grupo es un ítem de nivel superior; sus ubicaciones son hijos. Doble
clic en una ubicación emite `cloneActivated(group, location)` para que el
controlador abra el archivo y mueva el cursor.
"""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QPushButton, QSpinBox, QTreeWidget, QTreeWidgetItem,
    QVBoxLayout, QWidget,
)

from ..models.clone import CloneGroup, CloneLocation, CloneReport


class ClonePanel(QWidget):
    """Vista de árbol con botón de detección, mínimo de nodos y estado."""

    detectRequested = Signal(int)   # min_nodes
    cancelRequested = Signal()
    cloneActivated = Signal(object, object)   # (CloneGroup, CloneLocation)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("clonePanel")

        self.button = QPushButton("Detectar clones", self)
        self.button.setObjectName("cloneDetectButton")
        self.button.clicked.connect(
            lambda: self.detectRequested.emit(self.min_nodes.value())
        )

        self.cancel_button = QPushButton("Cancelar", self)
        self.cancel_button.setObjectName("cloneCancelButton")
        self.cancel_button.setEnabled(False)
        self.cancel_button.clicked.connect(self.cancelRequested.emit)

        self.min_nodes = QSpinBox(self)
        self.min_nodes.setObjectName("cloneMinNodes")
        self.min_nodes.setRange(1, 500)
        self.min_nodes.setValue(8)
        self.min_nodes.setPrefix("Mín. nodos: ")
        self.min_nodes.setToolTip(
            "Tamaño mínimo del fragmento en nodos del AST. "
            "Valores bajos detectan clones pequeños pero producen ruido; "
            "valores altos se enfocan en clones grandes."
        )

        self.status = QLabel("Sin detección. Pulsa «Detectar clones».", self)
        self.status.setObjectName("cloneStatus")
        self.status.setWordWrap(True)

        controls = QHBoxLayout()
        controls.addWidget(self.button)
        controls.addWidget(self.cancel_button)
        controls.addWidget(self.min_nodes)
        controls.addStretch(1)

        self.tree = QTreeWidget(self)
        self.tree.setObjectName("cloneTree")
        self.tree.setHeaderLabels(["Grupo", "Ubicación"])
        self.tree.setColumnWidth(0, 240)
        self.tree.itemDoubleClicked.connect(self._activate)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.addLayout(controls)
        layout.addWidget(self.status)
        layout.addWidget(self.tree, 1)

    def set_running(self, running: bool):
        """Deshabilita el botón durante la detección y habilita Cancelar."""
        self.button.setEnabled(not running)
        self.min_nodes.setEnabled(not running)
        self.cancel_button.setEnabled(running)

    def set_report(self, report: CloneReport | None, status: str):
        """Reemplaza el contenido del árbol con el nuevo reporte.

        Ordena los grupos por tipo (I antes que II) y luego por tamaño
        descendente, para que los clones más grandes aparezcan primero.
        """
        self.status.setText(status)
        self.tree.clear()
        if not report or not report.groups:
            return
        ordered = sorted(
            report.groups,
            key=lambda g: (g.clone_type, -g.size, g.hash),
        )
        for group in ordered:
            label = (
                f"Tipo {group.clone_type} — {group.size} nodos"
                f" ({len(group.locations)} ubicaciones)"
            )
            parent = QTreeWidgetItem(self.tree)
            parent.setText(0, label)
            parent.setText(1, "")
            parent.setData(0, Qt.ItemDataRole.UserRole, group)
            for location in group.locations:
                child = QTreeWidgetItem(parent)
                child.setText(0, location.file_path.name)
                child.setText(
                    1,
                    f"L{location.start_line + 1}:C{location.start_column + 1}"
                    f"–L{location.end_line + 1}:C{location.end_column + 1}"
                    f" ({location.node_count} nodos)",
                )
                child.setData(0, Qt.ItemDataRole.UserRole, (group, location))
            parent.setExpanded(True)

    def _activate(self, item: QTreeWidgetItem, _column: int):
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if isinstance(data, tuple) and len(data) == 2:
            group, location = data
            self.cloneActivated.emit(group, location)
