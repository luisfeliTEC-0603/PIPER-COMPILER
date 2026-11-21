"""Invocación y resultado inmutables de una ejecución de pipoc."""

from dataclasses import dataclass
from pathlib import Path
import shlex


@dataclass(frozen=True, slots=True)
class OptimizationOptions:
    """Selección del usuario; permitir spilling nunca obliga a usar memoria."""

    unroll: bool = False
    dce: bool = False
    allow_spill: bool = True

    @property
    def unroll_argument(self) -> str:
        return "auto" if self.unroll else "off"

    @property
    def arguments(self) -> tuple[str, ...]:
        args = ("--unroll", self.unroll_argument, "--spill", "on" if self.allow_spill else "off")
        return (*args, "--dce") if self.dce else args

    @property
    def description(self) -> str:
        return (f"Unrolling: {self.unroll_argument} · DCE: {'sí' if self.dce else 'no'} · "
                f"Permitir spilling: {'sí' if self.allow_spill else 'no'} · Scheduler: automático")


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
    optimizations: OptimizationOptions = OptimizationOptions()

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
    artifact_error: str | None = None

    @property
    def succeeded(self) -> bool:
        return self.exit_code == 0 and self.artifact_error is None
