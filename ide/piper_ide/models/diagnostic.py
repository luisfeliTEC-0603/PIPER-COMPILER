"""Diagnósticos producidos por pipoc y mostrados en el panel Problemas."""

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Diagnostic:
    """Ubicación basada en cero y mensaje literal emitido por el compilador."""

    line: int
    column: int
    message: str
    severity: str = "error"

    @property
    def location_label(self) -> str:
        """Devuelve la ubicación legible desde uno usada por la interfaz."""
        return f"L{self.line + 1}:C{self.column + 1}"
