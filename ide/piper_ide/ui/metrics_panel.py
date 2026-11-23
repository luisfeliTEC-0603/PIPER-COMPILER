"""Comparación interactiva de los contadores del compilador y el RTL."""

from pathlib import Path

from ..services.metrics import TIMED_ROUNDS

from PySide6.QtCore import Qt, Signal, QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QComboBox, QFrame, QGridLayout, QHBoxLayout, QLabel, QProgressBar,
    QPushButton, QScrollArea, QVBoxLayout, QWidget,
)

METRICS = (
    ("cycles", "Ciclos", "ciclos", "Tiempo de ejecución del programa en el RTL. Fuente: contador cpu_cycles de PORYGON."),
    ("instructions", "Instrucciones", "ops", "Operaciones estáticas del programa máquina. Se excluyen literales y relleno. Fuente: pipoc."),
    ("code_bytes", "Código en ROM", "bytes", "Cada bundle ocupa ocho bytes, incluido su relleno. Fuente: calendario VLIW de pipoc."),
    ("compile_ms", "Compilación (mediana)", "ms", f"Mediana de {TIMED_ROUNDS} compilaciones por variante después de un calentamiento. Base y Selección alternan el primer turno. Fuente: reloj monotónico de la IDE."),
    ("removed", "Eliminadas", "IR", "Instrucciones IR eliminadas por DCE, incluidas las de rutinas descartadas. Fuente: informe DCE."),
    ("reordered", "Reordenadas", "ops", "Operaciones emitidas antes que una operación precedente del mismo bloque. Fuente: ciclos de emisión del scheduler."),
    ("renamed", "Renombradas", "ops", "Operaciones de máquina cuyos registros físicos cambió Rename. Fuente: asignación de registros."),
)


def number(value):
    if value is None:
        return "No disponible"
    if isinstance(value, float):
        return f"{value:.2f}".replace(".", ",")
    return str(value)


class MetricCard(QFrame):
    selected = Signal(str)

    def __init__(self, metric):
        super().__init__()
        self.key, title, self.unit, description = metric
        self.setObjectName("metricCard")
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(description)
        layout = QGridLayout(self)
        layout.setContentsMargins(12, 7, 12, 8)
        layout.setVerticalSpacing(2)
        heading = QLabel(f"{title} ({self.unit})")
        heading.setObjectName("metricTitle")
        layout.addWidget(heading, 0, 0, 1, 3)
        self.rows = []
        for row, name in enumerate(("Base", "Selección"), 1):
            bar = QProgressBar()
            bar.setObjectName("baseBar" if row == 1 else "selectedBar")
            bar.setRange(0, 1000)
            bar.setFixedHeight(7)
            bar.setTextVisible(False)
            value = QLabel()
            value.setAlignment(Qt.AlignmentFlag.AlignRight)
            layout.addWidget(QLabel(name), row, 0)
            layout.addWidget(bar, row, 1)
            layout.addWidget(value, row, 2)
            self.rows.append((bar, value))
        layout.setColumnStretch(1, 1)

    def update_values(self, values):
        maximum = max((v for v in values if v is not None), default=0)
        for (bar, label), value in zip(self.rows, values):
            bar.setValue(round(1000 * value / maximum) if maximum and value is not None else 0)
            label.setText(number(value))
        self.setAccessibleName(f"{self.key}. Base {number(values[0])}. Selección {number(values[1])}.")

    def focusInEvent(self, event):
        super().focusInEvent(event)
        self.selected.emit(self.key)

    def mousePressEvent(self, event):
        self.setFocus()
        self.selected.emit(self.key)
        super().mousePressEvent(event)


class MetricsPanel(QScrollArea):
    choose_simulator = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.report = None
        self.setWidgetResizable(True)
        body = QWidget()
        self.setWidget(body)
        layout = QVBoxLayout(body)
        top = QHBoxLayout()
        self.heading = QLabel("Medí el archivo Piper activo para comparar sus opciones.")
        self.heading.setObjectName("metricsTitle")
        self.heading.setTextFormat(Qt.TextFormat.PlainText)
        self.heading.setWordWrap(True)
        top.addWidget(self.heading, 1)
        self.simulator = QPushButton("Elegir PORYGON…")
        self.simulator.clicked.connect(lambda: self.choose_simulator.emit())
        top.addWidget(self.simulator)
        layout.addLayout(top)
        self.status = QLabel()
        self.status.setTextFormat(Qt.TextFormat.PlainText)
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.chart = QWidget()
        chart = QVBoxLayout(self.chart)
        chart.setContentsMargins(0, 0, 0, 0)
        toolbar = QHBoxLayout()
        self.view = QComboBox()
        self.view.setAccessibleName("Vista de métricas")
        self.view.addItems(("Rendimiento", "Transformaciones"))
        toolbar.addWidget(self.view)
        toolbar.addStretch()
        folder = QPushButton("Abrir resultados")
        folder.clicked.connect(self.open_results)
        toolbar.addWidget(folder)
        chart.addLayout(toolbar)
        grid = QGridLayout()
        chart.addLayout(grid)
        self.cards = {}
        for i, metric in enumerate(METRICS):
            card = MetricCard(metric)
            self.cards[metric[0]] = card
            card.selected.connect(self.select_metric)
            position = i if i < 4 else i - 4
            grid.addWidget(card, position // 2, position % 2)
        self.detail = QLabel()
        self.detail.setWordWrap(True)
        chart.addWidget(self.detail)
        layout.addWidget(self.chart)
        layout.addStretch()
        self.view.currentIndexChanged.connect(self.change_view)
        self.chart.hide()

    def begin(self, source, options):
        self.report = None
        self.chart.hide()
        self.heading.setText(f"{source.name} · Base y Selección")
        self.status.setText("Preparando medición…")
        self.simulator.setEnabled(False)
        self.selection = options.description

    def finish(self, report):
        self.report = report
        self.heading.setText(f"{Path(report['source']).name} · Comparación de optimizaciones")
        base, selected = report["variants"].values()
        messages = ["Base · Unroll, DCE y Rename apagados · Spilling permitido",
                    "Selección · " + self.selection]
        if "r1" in base and "r1" in selected and not report["warning"]:
            messages.append(f"Resultado en r1 · {base['r1']} en ambas ejecuciones")
        if report["warning"]:
            messages.append(report["warning"])
        for name, item in report["variants"].items():
            if item["error"]:
                messages.append(f"{name} · {item['error']}")
        self.status.setText("\n".join(messages))
        for key, card in self.cards.items():
            card.update_values([v["values"].get(key) for v in (base, selected)])
        self.chart.show()
        self.view.setCurrentIndex(0)
        self.change_view(0)

    def change_view(self, index):
        for i, card in enumerate(self.cards.values()):
            card.setVisible((i >= 4) == bool(index))
        self.select_metric("removed" if index else "cycles")

    def select_metric(self, key):
        if not self.report:
            return
        for name, card in self.cards.items():
            card.setProperty("active", name == key)
            card.style().unpolish(card)
            card.style().polish(card)
        _, title, unit, description = next(metric for metric in METRICS if metric[0] == key)
        before, after = (v["values"].get(key) for v in self.report["variants"].values())
        difference = "Diferencia no disponible"
        if before is not None and after is not None:
            delta = after - before
            difference = f"Δ {number(delta)} {unit}"
            if before:
                difference += f" ({100 * delta / before:+.1f} %)".replace(".", ",")
        self.detail.setText(f"{title} · {difference}\n{description}")

    def open_results(self):
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(self.report["path"]).parent)))

    def apply_theme(self, colors):
        self.setStyleSheet("""
            QLabel#metricsTitle { font-size: 15px; font-weight: bold; }
            QLabel#metricTitle { font-weight: bold; }
            QFrame#metricCard { background: %(surface)s; border: 1px solid %(border)s; border-radius: 7px; }
            QFrame#metricCard[active="true"] { border: 2px solid %(accent)s; }
            QFrame#metricCard QLabel { background: transparent; }
            QProgressBar { background: %(raised)s; border: 0; border-radius: 3px; }
            QProgressBar#baseBar::chunk { background: %(muted)s; border-radius: 3px; }
            QProgressBar#selectedBar::chunk { background: %(accent)s; border-radius: 3px; }
        """ % colors)
