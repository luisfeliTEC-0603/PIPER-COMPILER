"""Diagnósticos producidos por pipoc y mostrados en el panel Problemas."""

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class Diagnostic:
    """Error de una versión concreta de un buffer, sin dependencia de Qt.

    Líneas y columnas son índices desde cero de caracteres Unicode de Python;
    el final del rango es exclusivo; un rango vacío indica dónde falta insertar
    un símbolo. services/diagnostics convierte los bytes
    UTF-8 de C y CodeEditor convierte estos índices a las posiciones UTF-16 de
    Qt. La identidad evita navegar al archivo equivocado desde Problemas.
    """

    line: int
    column: int
    message: str
    severity: str = "error"
    end_line: int | None = None
    end_column: int | None = None
    phase: str = ""
    document_id: str = ""
    revision: int = -1
    source: Path | None = None
    display_name: str = ""
    has_location: bool = True

    @property
    def location_label(self) -> str:
        """Devuelve la ubicación legible desde uno usada por la interfaz."""
        return f"L{self.line + 1}:C{self.column + 1}" if self.has_location else "Programa"

    @property
    def phase_label(self) -> str:
        """Traduce la fase del protocolo sin modificar el mensaje original."""
        return {"lexical": "Léxico", "syntax": "Sintaxis", "semantic": "Semántica"}.get(self.phase, "Error")
