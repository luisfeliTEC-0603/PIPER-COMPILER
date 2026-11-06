"""Modelo de resultados de detección de clones.

Un `CloneGroup` agrupa ubicaciones idénticas (Tipo I o Tipo II). El
controlador los construye a partir del JSON emitido por `pipoc --clones`.
El editor los usa para resaltar; el panel para navegar.
"""

from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class CloneLocation:
    """Un fragmento concreto en un archivo."""
    file_path: Path
    start_line: int          # base 0
    start_column: int        # base 0
    end_line: int            # base 0
    end_column: int          # base 0
    node_count: int

    @property
    def display(self) -> str:
        """Texto compacto para el panel, con base 1 para humanos."""
        return f"{self.file_path.name}:{self.start_line + 1} ({self.node_count} nodos)"


@dataclass(frozen=True)
class CloneGroup:
    """Conjunto de fragmentos idénticos (≥ 2 ubicaciones)."""
    mode: str                # "exact" o "renamed"
    hash: str                # huella hexadecimal
    locations: tuple[CloneLocation, ...]

    @property
    def clone_type(self) -> str:
        """Etiqueta corta para el panel: 'I' o 'II'."""
        return "I" if self.mode == "exact" else "II"

    @property
    def size(self) -> int:
        """Tamaño en nodos del fragmento (el del primer location)."""
        return self.locations[0].node_count if self.locations else 0


@dataclass
class CloneReport:
    """Resultado completo de una detección en un proyecto o archivo."""
    groups: list[CloneGroup] = field(default_factory=list)
    files_analyzed: int = 0
    elapsed_ms: int = 0
    mode: str = "renamed"
    min_nodes: int = 8

    @property
    def total_locations(self) -> int:
        return sum(len(g.locations) for g in self.groups)
