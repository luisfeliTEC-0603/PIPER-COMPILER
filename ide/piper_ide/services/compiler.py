"""Construcción y ejecución asíncrona de pipoc para el documento activo."""

import os
from pathlib import Path
import re
import shutil

from PySide6.QtCore import QObject, Signal

from ..infrastructure.processes import ProcessRunner
from ..models.compilation import CompilationInvocation, CompilationResult
from ..models.diagnostic import Diagnostic


FORMATS = ("bin", "asm", "sch", "mem", "ins", "all")


class CompilerConfigurationError(RuntimeError):
    """La compilación no puede comenzar por una ruta o formato inválido."""


def find_compiler(project_root: Path) -> Path:
    """Busca Pipoc junto al proyecto, sus padres, el código del IDE o el PATH.

    Permite iniciar el IDE desde otra carpeta o abrir directamente un archivo
    bajo tests/codegen. El IDE y Pipoc usan las rutas del mismo entorno Linux/WSL.
    """
    root = project_root.resolve()
    cwd = Path.cwd()
    roots = dict.fromkeys((root, *root.parents, cwd, *cwd.parents,
                          Path(__file__).resolve().parents[3]))
    for directory in roots:
        candidate = directory / "bin" / "pipoc"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate.resolve()
    resolved = shutil.which("pipoc")
    if resolved:
        return Path(resolved).resolve()
    raise CompilerConfigurationError(
        "No se encontró pipoc. Ejecutá 'make all' en la raíz del compilador."
    )


def build_invocation(source: Path, project_root: Path,
                     output_format: str) -> CompilationInvocation:
    """Resuelve rutas explícitas bajo build/ide sin escribir todavía."""
    source = source.resolve()
    project_root = project_root.resolve()
    if output_format not in FORMATS:
        raise CompilerConfigurationError(f"Formato de salida desconocido: {output_format}")
    if source.suffix.lower() != ".rwt":
        raise CompilerConfigurationError("Pipoc solo compila documentos Piper con extensión .rwt.")
    try:
        relative = source.relative_to(project_root).with_suffix("")
    except ValueError:
        relative = Path(source.stem)
    base = project_root / "build" / "ide" / relative
    output_dir = base.parent
    compiler = find_compiler(project_root)
    arguments: list[str] = [] if output_format == "bin" else [output_format]
    artifacts: list[Path] = []
    extension = {"bin": ".bin", "asm": ".asm", "sch": ".bundles", "mem": ".mem"}
    if output_format in extension:
        primary = base.with_suffix(extension[output_format])
        arguments.extend(("-o", str(primary)))
        artifacts.append(primary)
    if output_format in ("ins", "all"):
        ast = base.with_suffix(".ast.json")
        symbols = base.with_suffix(".symbols.txt")
        arguments.extend(("--ast-json", str(ast), "--emit-symbols", str(symbols)))
        artifacts.extend((ast, symbols))
    if output_format == "all":
        binary = base.with_suffix(".bin")
        assembly = base.with_suffix(".asm")
        bundles = base.with_suffix(".bundles")
        memory = base.with_suffix(".mem")
        arguments[1:1] = ("-o", str(binary))
        arguments.extend(("--emit-asm", str(assembly),
                          "--emit-bundles", str(bundles),
                          "--emit-mem", str(memory)))
        artifacts[0:0] = (binary, assembly, bundles, memory)
    arguments.extend(("-v", str(source)))
    return CompilationInvocation(
        compiler, source, project_root, output_dir, output_format,
        tuple(arguments), tuple(artifacts),
    )


_LOCATED = re.compile(r"^(?:(?:.+?):)?(\d+):(\d+):\s*(.+)$")
_SEMANTIC = re.compile(r"^\[Error Semántico\] Línea (\d+):\s*(.+)$")


def parse_diagnostics(stderr: str) -> tuple[Diagnostic, ...]:
    """Interpreta errores léxicos, sintácticos y semánticos de pipoc."""
    diagnostics: list[Diagnostic] = []
    generic: list[str] = []
    for raw_line in stderr.splitlines():
        line = raw_line.strip()
        located = _LOCATED.match(line)
        semantic = _SEMANTIC.match(line)
        if located:
            diagnostics.append(Diagnostic(
                max(0, int(located.group(1)) - 1),
                max(0, int(located.group(2)) - 1),
                located.group(3),
            ))
        elif semantic:
            diagnostics.append(Diagnostic(
                max(0, int(semantic.group(1)) - 1), 0, semantic.group(2),
            ))
        elif "error:" in line.casefold() and not line.startswith("[pipoc]"):
            generic.append(line)
    if not diagnostics and generic:
        diagnostics.append(Diagnostic(0, 0, generic[0]))
    return tuple(diagnostics)


class CompilerService(QObject):
    """Coordina una compilación a la vez y publica resultados tipados."""

    started = Signal(object)
    outputReceived = Signal(str, str)
    finished = Signal(object)
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.runner = ProcessRunner(self)
        self.runner.outputReceived.connect(self.outputReceived.emit)
        self.runner.completed.connect(self._completed)
        self.runner.failed.connect(self._failed)
        self._invocation: CompilationInvocation | None = None

    @property
    def running(self) -> bool:
        return self.runner.running

    def compile(self, source: Path, project_root: Path, output_format: str) -> bool:
        if self.running:
            return False
        invocation = build_invocation(source, project_root, output_format)
        invocation.output_dir.mkdir(parents=True, exist_ok=True)
        self._invocation = invocation
        if not self.runner.start(invocation.compiler, invocation.arguments,
                                 invocation.project_root):
            self._invocation = None
            return False
        self.started.emit(invocation)
        return True

    def stop(self):
        self.runner.stop()

    def _completed(self, exit_code: int, stdout: str, stderr: str):
        invocation, self._invocation = self._invocation, None
        if invocation is None:
            return
        result = CompilationResult(
            invocation, exit_code, stdout, stderr, parse_diagnostics(stderr)
        )
        self.finished.emit(result)

    def _failed(self, message: str):
        self._invocation = None
        self.failed.emit(message)
