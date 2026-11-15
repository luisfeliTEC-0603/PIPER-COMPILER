"""Ejecución asíncrona sin shell, con captura incremental de ambos canales."""

from pathlib import Path

from PySide6.QtCore import QObject, QProcess, Signal


class ProcessRunner(QObject):
    """Envuelve un único QProcess reutilizable y conserva su salida completa."""

    outputReceived = Signal(str, str)
    completed = Signal(int, str, str)
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self._read_stdout)
        self.process.readyReadStandardError.connect(self._read_stderr)
        self.process.finished.connect(self._finished)
        self.process.errorOccurred.connect(self._error)
        self._stdout: list[str] = []
        self._stderr: list[str] = []
        self._failed_to_start = False

    @property
    def running(self) -> bool:
        return self.process.state() != QProcess.ProcessState.NotRunning

    def start(self, executable: Path, arguments: tuple[str, ...], cwd: Path) -> bool:
        """Inicia el ejecutable directamente; nunca interpone un shell."""
        if self.running:
            return False
        self._stdout.clear()
        self._stderr.clear()
        self._failed_to_start = False
        self.process.setProgram(str(executable))
        self.process.setArguments(list(arguments))
        self.process.setWorkingDirectory(str(cwd))
        self.process.start()
        return True

    def stop(self):
        """Detiene el hijo durante el cierre de la aplicación."""
        if not self.running:
            return
        self.process.terminate()
        if not self.process.waitForFinished(750):
            self.process.kill()
            self.process.waitForFinished(750)

    def _read_stdout(self):
        text = bytes(self.process.readAllStandardOutput()).decode("utf-8", "replace")
        if text:
            self._stdout.append(text)
            self.outputReceived.emit("stdout", text)

    def _read_stderr(self):
        text = bytes(self.process.readAllStandardError()).decode("utf-8", "replace")
        if text:
            self._stderr.append(text)
            self.outputReceived.emit("stderr", text)

    def _finished(self, exit_code, _exit_status):
        self._read_stdout()
        self._read_stderr()
        if not self._failed_to_start:
            self.completed.emit(exit_code, "".join(self._stdout), "".join(self._stderr))

    def _error(self, error):
        if error == QProcess.ProcessError.FailedToStart:
            self._failed_to_start = True
            self.failed.emit(self.process.errorString())
