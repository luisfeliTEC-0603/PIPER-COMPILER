"""Revisión asíncrona de buffers con el frontend C de Pipoc.

Python no decide si el lenguaje es válido: ejecuta ``pipoc --check
--diagnostics-json`` sobre una copia temporal y decodifica su reporte. Este
servicio desconoce los widgets y nunca guarda el archivo del usuario.
"""

from dataclasses import dataclass
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from PySide6.QtCore import QObject, QProcess, QTimer, Signal

from ..models.diagnostic import Diagnostic
from .compiler import find_compiler


@dataclass(frozen=True, slots=True)
class DiagnosticSnapshot:
    """Texto e identidad capturados antes de iniciar un proceso independiente."""

    document_id: str
    revision: int
    generation: int
    text: str
    source: Path | None
    display_name: str
    project_root: Path


@dataclass(frozen=True, slots=True)
class DiagnosticResult:
    """Respuesta del frontend junto a la copia exacta que fue analizada."""

    snapshot: DiagnosticSnapshot
    valid: bool
    diagnostics: tuple[Diagnostic, ...]


def decode_report(stdout: str, snapshot: DiagnosticSnapshot) -> DiagnosticResult:
    """Valida el protocolo y traduce coordenadas UTF-8 a caracteres Unicode.

    El lexer cuenta bytes desde uno. El IDE conserva caracteres desde cero;
    esta conversión evita desplazar un error después de acentos o emojis en
    comentarios. Un error global con ubicación 0:0 se lista sin subrayado.
    Un reporte incompleto se rechaza: nunca equivale a «sin errores».
    """
    report = json.loads(stdout)
    if (not isinstance(report, dict) or report.get("version") != 1
            or report.get("column_unit") != "utf8-byte"
            or type(report.get("valid")) is not bool
            or not isinstance(report.get("diagnostics"), list)):
        raise ValueError("Formato de diagnósticos incompatible; ejecutá 'make all'.")
    lines = snapshot.text.split("\n")

    def position(value):
        if (not isinstance(value, dict)
                or type(value.get("line")) is not int
                or type(value.get("column")) is not int):
            raise ValueError("Ubicación inválida en el reporte de Pipoc.")
        line, column = value["line"], value["column"]
        if line == 0 and column == 0:
            return None
        if not 1 <= line <= len(lines) or column < 1:
            raise ValueError("Ubicación fuera del buffer analizado.")
        encoded = lines[line - 1].encode("utf-8")
        if column > len(encoded) + 1:
            raise ValueError("Columna fuera del buffer analizado.")
        return line - 1, len(encoded[:column - 1].decode("utf-8", errors="ignore"))

    diagnostics = []
    for entry in report["diagnostics"]:
        if (not isinstance(entry, dict)
                or entry.get("phase") not in ("lexical", "syntax", "semantic")
                or entry.get("severity") != "error"
                or not isinstance(entry.get("message"), str)):
            raise ValueError("Diagnóstico inválido en el reporte de Pipoc.")
        start, end = position(entry.get("start")), position(entry.get("end"))
        if (start is None) != (end is None) or (start and end < start):
            raise ValueError("Rango inválido en el reporte de Pipoc.")
        diagnostics.append(Diagnostic(
            line=start[0] if start else 0, column=start[1] if start else 0,
            message=entry["message"], severity=entry["severity"],
            end_line=end[0] if end else None, end_column=end[1] if end else None,
            phase=entry["phase"], document_id=snapshot.document_id,
            revision=snapshot.revision, source=snapshot.source,
            display_name=snapshot.display_name, has_location=start is not None,
        ))
    if report["valid"] != (len(diagnostics) == 0):
        raise ValueError("Reporte inconsistente: validez y errores no coinciden.")
    return DiagnosticResult(snapshot, report["valid"], tuple(diagnostics))


@dataclass
class _Job:
    """Recursos que deben sobrevivir hasta que el proceso deje de leer el temporal."""

    snapshot: DiagnosticSnapshot
    directory: TemporaryDirectory
    timer: QTimer


class DiagnosticService(QObject):
    """Ejecuta revisiones cancelables sin bloquear el bucle de eventos de Qt."""

    ready = Signal(object)
    failed = Signal(object, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._active: QProcess | None = None
        self._jobs: dict[QProcess, _Job] = {}

    def request(self, snapshot: DiagnosticSnapshot):
        """Escribe una copia UTF-8 y lanza C; stdout queda reservado para JSON."""
        self.cancel()
        directory = None
        try:
            compiler = find_compiler(snapshot.project_root)
            directory = TemporaryDirectory(prefix="piper-check-")
            source = Path(directory.name) / "buffer.rwt"
            source.write_text(snapshot.text, encoding="utf-8", newline="\n")
        except (OSError, RuntimeError, UnicodeError) as error:
            if directory is not None:
                directory.cleanup()
            self.failed.emit(snapshot, str(error))
            return
        process = QProcess(self)
        timer = QTimer(process)
        timer.setSingleShot(True)
        timer.setInterval(8000)
        self._jobs[process] = _Job(snapshot, directory, timer)
        self._active = process
        process.setWorkingDirectory(str(snapshot.project_root))
        process.finished.connect(lambda code, status: self._finished(process, code, status))
        process.errorOccurred.connect(lambda error: self._process_error(process, error))
        timer.timeout.connect(lambda: self._timeout(process))
        process.start(str(compiler), ["--check", "--diagnostics-json", str(source)])
        timer.start()

    def cancel(self):
        """Invalida primero la respuesta; kill no espera y permite seguir escribiendo.

        El temporal se libera en finished, cuando C ya no puede estar leyéndolo.
        Los procesos cancelados conservan conexiones para completar su limpieza.
        """
        process, self._active = self._active, None
        if process is not None:
            process.kill()

    def _release(self, process):
        job = self._jobs.pop(process)
        job.timer.stop()
        job.directory.cleanup()
        process.deleteLater()
        return job

    def _finished(self, process, code, status):
        if process not in self._jobs:
            return
        current = process is self._active
        if current:
            self._active = None
        stdout = bytes(process.readAllStandardOutput()).decode("utf-8", errors="replace")
        stderr = bytes(process.readAllStandardError()).decode("utf-8", errors="replace")
        job = self._release(process)
        if not current:
            return
        try:
            if status != QProcess.ExitStatus.NormalExit or code not in (0, 1):
                detail = stderr.strip().splitlines()
                raise ValueError(detail[0] if detail else f"Pipoc terminó con código {code}.")
            result = decode_report(stdout, job.snapshot)
            if result.valid != (code == 0):
                raise ValueError("El código de salida contradice el reporte de Pipoc.")
        except (ValueError, TypeError) as error:
            self.failed.emit(job.snapshot, f"No se pudo revisar el programa: {error}")
            return
        self.ready.emit(result)

    def _process_error(self, process, error):
        # Un fallo de arranque no emite finished. Los demás errores sí terminan
        # por esa ruta; liberarlos aquí también produciría una doble limpieza.
        if error != QProcess.ProcessError.FailedToStart or process not in self._jobs:
            return
        current = process is self._active
        if current:
            self._active = None
        detail = process.errorString()
        job = self._release(process)
        if current:
            self.failed.emit(job.snapshot, "No se pudo iniciar Pipoc: " + detail)

    def _timeout(self, process):
        if process is self._active:
            snapshot = self._jobs[process].snapshot
            self.cancel()
            self.failed.emit(snapshot, "Pipoc excedió el tiempo de revisión (8 segundos).")

    def stop(self):
        """Cierra procesos y temporales al salir; solo aquí se espera brevemente."""
        self._active = None
        for process in list(self._jobs):
            process.blockSignals(True)
            process.kill()
            process.waitForFinished(250)
            self._release(process)
