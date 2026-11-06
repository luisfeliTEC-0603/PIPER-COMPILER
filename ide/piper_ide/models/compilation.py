"""Invocación y resultado inmutables de una ejecución de pipoc."""

from dataclasses import dataclass
from pathlib import Path
import shlex


@dataclass(frozen=True, slots=True)
class CompilationInvocation:
    """Comando resuelto y artefactos que debe producir."""

    compiler: Path
    source: Path
    project_root: Path
    output_dir: Path
    output_format: str
    arguments: tuple[str, ...]
    artifacts: tuple[Path, ...]

    @property
    def display_command(self) -> str:
        """Representación segura y copiable para la terminal del IDE."""
        return shlex.join((str(self.compiler), *self.arguments))


@dataclass(frozen=True, slots=True)
class CompilationResult:
    """Salida y estado de compilación; la revisión estructurada tiene su servicio."""

    invocation: CompilationInvocation
    exit_code: int
    stdout: str
    stderr: str

    @property
    def succeeded(self) -> bool:
        return self.exit_code == 0
