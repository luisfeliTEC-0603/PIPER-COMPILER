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
    rename: bool = False
    unroll_mode: str = "auto"
    unroll_factor: int = 2

    def __post_init__(self) -> None:
        if self.unroll_mode not in ("auto", "manual"):
            raise ValueError("El modo de unrolling debe ser automático o manual.")
        if type(self.unroll_factor) is not int or not 2 <= self.unroll_factor <= 4096:
            raise ValueError("El factor de unrolling debe ser un entero entre 2 y 4096.")

    @property
    def unroll_argument(self) -> str:
        if not self.unroll:
            return "off"
        return "auto" if self.unroll_mode == "auto" else str(self.unroll_factor)

    @property
    def arguments(self) -> tuple[str, ...]:
        args = ("--unroll", self.unroll_argument, "--spill", "on" if self.allow_spill else "off")
        if self.dce:
            args += ("--dce",)
        if self.rename:
            args += ("--rename",)
        return args

    @property
    def description(self) -> str:
        unrolling = ("desactivado" if not self.unroll else "automático"
                     if self.unroll_mode == "auto" else f"manual (factor {self.unroll_factor})")
        return (f"Unrolling: {unrolling} · DCE: {'sí' if self.dce else 'no'} · "
                f"Rename: {'sí' if self.rename else 'no'} · "
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
