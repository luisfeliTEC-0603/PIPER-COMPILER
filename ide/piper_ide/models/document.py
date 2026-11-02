"""Metadatos de un documento, independientes de los widgets del editor.

El texto y Deshacer pertenecen a QTextDocument en ui/documents.py. Aquí solo
se guarda lo necesario para identificar el archivo y escribirlo sin cambiar
su codificación ni sus finales de línea de manera accidental.
"""

from dataclasses import dataclass, field
from pathlib import Path
from uuid import uuid4


@dataclass
class Document:
    """Identidad estable y datos de disco de un buffer abierto.

    ``path=None`` identifica un archivo nuevo. ``disk_digest`` es la huella de
    la última lectura/escritura y permite advertir cambios hechos fuera del IDE.
    La identidad no cambia al guardar con otro nombre.
    """

    name: str
    path: Path | None = None
    encoding: str = "utf-8"
    newline: str = "\n"
    disk_digest: str | None = None
    read_only: bool = False
    generated: bool = False
    id: str = field(default_factory=lambda: uuid4().hex)

    @property
    def language(self) -> str:
        """Selecciona el resaltado por extensión; otros archivos siguen siendo texto."""
        suffix = Path(self.name).suffix.lower()
        return {".rwt": "piper", ".asm": "asm", ".json": "json",
                ".bundles": "asm", ".mem": "memory"}.get(suffix, "text")

    @property
    def language_label(self) -> str:
        """Nombre legible del formato para la cabecera y la barra de estado."""
        return {"piper": "Piper", "asm": "ASM PORYGON", "json": "JSON",
                "memory": "Memoria hexadecimal", "text": "Texto"}[self.language]
