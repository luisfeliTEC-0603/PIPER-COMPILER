"""Carpeta de trabajo y ubicación de sus archivos generados."""

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Project:
    """Raíz elegida por el usuario; build se presenta aparte en Generados."""

    root: Path

    @property
    def generated_root(self) -> Path:
        """Directorio que se podrá actualizar tras conectar la compilación."""
        return self.root / "build"

    def is_generated(self, path: Path) -> bool:
        """Reconoce salidas bajo build usando rutas reales, no prefijos de texto."""
        return path.resolve().is_relative_to(self.generated_root.resolve())
