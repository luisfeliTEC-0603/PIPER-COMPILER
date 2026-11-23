"""Resumen de la última compilación y accesos a sus archivos de texto."""

from PySide6.QtCore import Qt, Signal, QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QPlainTextEdit,
    QPushButton, QVBoxLayout, QWidget,
)

from ..services.artifacts import report_text, visible_artifacts


class CompilationPanel(QWidget):
    artifactRequested = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        self.summary = QPlainTextEdit()
        self.summary.setReadOnly(True)
        self.summary.setAccessibleName("Resultado de las optimizaciones")
        self.summary.setPlainText("Los resultados aparecerán después de compilar.")
        layout.addWidget(self.summary, 3)
        files = QVBoxLayout()
        files.addWidget(QLabel("Archivos de esta compilación"))
        self.artifacts = QListWidget()
        self.artifacts.setAccessibleName("Archivos de la última compilación")
        self.artifacts.itemDoubleClicked.connect(self._open_item)
        files.addWidget(self.artifacts)
        self.open_button = QPushButton("Abrir archivo")
        self.open_button.setEnabled(False)
        self.open_button.clicked.connect(lambda: self._open_item(self.artifacts.currentItem()))
        self.artifacts.currentItemChanged.connect(self._selection_changed)
        files.addWidget(self.open_button)
        self.folder_button = QPushButton("Abrir carpeta de resultados")
        self.folder_button.setEnabled(False)
        self.folder_button.clicked.connect(self._open_folder)
        files.addWidget(self.folder_button)
        self.output_directory = None
        layout.addLayout(files, 2)

    def begin(self, invocation):
        self.artifacts.clear()
        self.open_button.setEnabled(False)
        self.folder_button.setEnabled(False)
        self.output_directory = None
        self.summary.setPlainText("Compilando…\n" + (
            "Solo AST y símbolos." if invocation.output_format == "ins"
            else invocation.optimizations.description))

    def finish(self, result):
        self.summary.setPlainText(report_text(result))
        self.output_directory = result.invocation.output_dir if result.succeeded else None
        self.folder_button.setEnabled(result.succeeded)
        for path in visible_artifacts(result):
            item = QListWidgetItem(path.name)
            item.setData(Qt.ItemDataRole.UserRole, path)
            item.setToolTip(str(path) if path.suffix not in (".bin", ".zip") else str(path) + "\nUsá Abrir carpeta de resultados para ubicar este archivo.")
            self.artifacts.addItem(item)

    def fail(self, message):
        self.artifacts.clear()
        self.open_button.setEnabled(False)
        self.folder_button.setEnabled(False)
        self.output_directory = None
        self.summary.setPlainText("No se pudo iniciar pipoc:\n" + message)

    def _selection_changed(self, current, _previous):
        path = current.data(Qt.ItemDataRole.UserRole) if current else None
        self.open_button.setEnabled(bool(path and path.suffix not in (".bin", ".zip")))

    def _open_item(self, item):
        if item is not None:
            path = item.data(Qt.ItemDataRole.UserRole)
            if path.suffix not in (".bin", ".zip"):
                self.artifactRequested.emit(path)

    def _open_folder(self):
        if self.output_directory is not None:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.output_directory)))
