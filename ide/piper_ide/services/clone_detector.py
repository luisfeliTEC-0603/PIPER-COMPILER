"""Ejecuta `pipoc --clones` y convierte su JSON en objetos Python.

El servicio corre un QProcess por archivo y emite `ready` con el
`CloneReport` parcial. La agrupación entre archivos la hace el controlador.
"""

import json
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, Signal

from ..models.clone import CloneGroup, CloneLocation, CloneReport
from ..services.compiler import CompilerConfigurationError, find_compiler


class CloneDetectorService(QObject):
    """Lanza `pipoc --clones` y emite el resultado parseado de un archivo."""

    ready = Signal(str, str, object)   # (ruta, modo, CloneReport parcial)
    failed = Signal(str, str, str)     # (ruta, modo, mensaje de error)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._pending: dict[QProcess, tuple[Path, str]] = {}

    def request(self, source_path: Path, project_root: Path,
                min_nodes: int, mode: str) -> bool:
        """Lanza la detección para un archivo en el modo indicado."""
        try:
            compiler = find_compiler(project_root)
        except CompilerConfigurationError as error:
            self.failed.emit(str(source_path), mode, str(error))
            return False

        arguments = ["--clones", str(min_nodes)]
        arguments.append("--clones-exact" if mode == "exact" else "--clones-renamed")
        arguments.append(str(source_path))

        process = QProcess(self)
        process.setProgram(str(compiler))
        process.setArguments(arguments)
        process.setWorkingDirectory(str(project_root))
        process.finished.connect(
            lambda exit_code, _status, p=process, path=source_path, m=mode:
                self._finished(p, path, m, exit_code)
        )
        process.errorOccurred.connect(
            lambda error, p=process, path=source_path, m=mode:
                self._process_error(p, path, m, error)
        )
        self._pending[process] = (source_path, mode)
        process.start()
        return True

    def _finished(self, process: QProcess, source_path: Path, mode: str,
                  exit_code: int):
        stdout = bytes(process.readAllStandardOutput()).decode("utf-8", "replace")
        stderr = bytes(process.readAllStandardError()).decode("utf-8", "replace")
        self._pending.pop(process, None)
        process.deleteLater()

        if exit_code != 0:
            message = stderr.strip() or f"pipoc terminó con código {exit_code}"
            self.failed.emit(str(source_path), mode, message)
            return

        try:
            payload = json.loads(stdout)
        except json.JSONDecodeError as error:
            self.failed.emit(str(source_path), mode, f"JSON inválido: {error}")
            return

        report = self._parse(payload, source_path, mode)
        self.ready.emit(str(source_path), mode, report)

    def _process_error(self, process: QProcess, source_path: Path, mode: str,
                       error):
        if error != QProcess.ProcessError.FailedToStart:
            return
        self._pending.pop(process, None)
        process.deleteLater()
        self.failed.emit(str(source_path), mode, process.errorString())

    def _parse(self, payload: dict, source_path: Path, mode: str) -> CloneReport:
        groups: list[CloneGroup] = []
        for raw_group in payload.get("groups", []):
            locations = []
            for raw in raw_group.get("locations", []):
                locations.append(CloneLocation(
                    file_path=source_path,
                    start_line=max(0, raw["start_line"] - 1),
                    start_column=max(0, raw["start_column"] - 1),
                    end_line=max(0, raw["end_line"] - 1),
                    end_column=max(0, raw["end_column"] - 1),
                    node_count=raw.get("node_count", 0),
                ))
            if len(locations) >= 2:
                groups.append(CloneGroup(
                    mode=mode,
                    hash=raw_group["hash"],
                    locations=tuple(locations),
                ))
        return CloneReport(
            groups=groups,
            files_analyzed=1,
            mode=mode,
            min_nodes=payload.get("min_nodes", 8),
        )

    def cancel(self):
        for process in list(self._pending):
            process.blockSignals(True)
            if process.state() != QProcess.ProcessState.NotRunning:
                process.kill()
            process.deleteLater()
            self._pending.pop(process, None)

    def stop(self):
        self.cancel()
