"""Iconos SVG de contorno convertidos a QIcon con el color del tema.

Los trazos simples se definen aquí. La Pokéball usa el recurso SVG del paquete.
El mismo dibujo se rasteriza a varios tamaños para los controles de la ventana.
"""

from pathlib import Path

from PySide6.QtCore import QByteArray, Qt
from PySide6.QtGui import QIcon, QPainter, QPixmap
from PySide6.QtSvg import QSvgRenderer

from .theme import resource_path


_PATHS = {
    "save": '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12l4 4v12a2 2 0 0 1-2 2Z"/><path d="M17 21v-8H7v8M7 3v5h9V3"/>',
    "play": '<path d="m8 5 11 7-11 7Z"/>',
    "plus": '<path d="M12 5v14M5 12h14"/>',
    "refresh": '<path d="M20 11a8 8 0 0 0-14-5L3 9m0-6v6h6M4 13a8 8 0 0 0 14 5l3-3m0 6v-6h-6"/>',
    "settings": '<path d="M3 6h18M3 18h18"/><circle cx="9" cy="6" r="3"/><circle cx="15" cy="18" r="3"/>',
    "close": '<path d="m6 6 12 12M6 18 18 6"/>',
}


def line_icon(name: str, color: str) -> QIcon:
    """Crea un icono por nombre con un color de trazo, por ejemplo ``#40bb98``.

    ``name`` debe existir en _PATHS o ser ``pokeball``. El fondo es transparente.
    QApplication debe existir antes de llamar: QPixmap necesita el entorno GUI.
    Al cambiar de tema hay que recrear el icono con el nuevo color.
    """
    if name == "pokeball":
        svg = Path(resource_path("icons/pokeball.svg")).read_text(encoding="utf-8")
        svg = svg.replace("currentColor", color)
    else:
        svg = (
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
            f'fill="none" stroke="{color}" stroke-width="1.6" '
            f'stroke-linecap="round" stroke-linejoin="round">{_PATHS[name]}</svg>'
        )
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    icon = QIcon()
    # Qt elegirá la representación más cercana al tamaño solicitado por el botón.
    for size in (16, 24, 32, 48):
        pixmap = QPixmap(size, size)
        pixmap.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pixmap)
        renderer.render(painter)
        painter.end()
        icon.addPixmap(pixmap)
    return icon
