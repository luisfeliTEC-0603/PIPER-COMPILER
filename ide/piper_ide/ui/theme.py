"""Recursos y estilo común para Rowlet oscuro y Rowlet shiny.

Los JSON contienen colores por función (fondo, texto, acento, error…). QPalette
cubre los controles nativos y QSS define las zonas específicas de la interfaz.
Este módulo no conserva preferencias ni modifica documentos del editor.
"""

import json
from importlib.resources import files

from PySide6.QtGui import QColor, QFont, QFontDatabase, QPalette


THEME_IDS = ("rowlet-oscuro", "rowlet-shiny")


def resource_path(relative: str) -> str:
    """Resuelve una ruta interna de resources para las API de Qt que piden archivos.

    La ruta parte del paquete instalado, no del directorio desde el que se abrió
    el IDE. ``relative`` es una ruta de recurso propia, como images/rowlet.png.
    """
    return str(files("piper_ide").joinpath("resources", relative))


def load_theme(theme_id: str) -> dict[str, str]:
    """Carga los colores de un tema permitido; rechaza identificadores desconocidos.

    La lista THEME_IDS limita la selección a los dos archivos distribuidos con
    el IDE. Una lectura fallida se propaga para no ocultar recursos incompletos.
    """
    if theme_id not in THEME_IDS:
        raise ValueError(f"Tema desconocido: {theme_id}")
    return json.loads(
        files("piper_ide").joinpath("resources", "themes", f"{theme_id}.json")
        .read_text(encoding="utf-8")
    )


def monospace_font(size: int) -> QFont:
    """Devuelve una fuente monoespaciada disponible con tamaño en píxeles.

    Prioriza las familias usadas en el diseño y recurre a la fuente fija del
    sistema si no hay ninguna. Requiere que QApplication ya esté creada.
    """
    available = QFontDatabase.families()
    font = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
    for family in ("Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Menlo"):
        if family in available:
            font.setFamily(family)
            break
    font.setPixelSize(size)
    return font


def palette_for(t: dict[str, str]) -> QPalette:
    """Convierte los colores semánticos del tema a roles nativos de Qt.

    Incluye estados deshabilitados y sombras de controles: QSS por sí solo no
    cubre todas las partes que dibuja el estilo nativo, como menús y selectores.
    """
    palette = QPalette()
    for role, key in (
        (QPalette.ColorRole.Window, "bg"),
        (QPalette.ColorRole.WindowText, "text"),
        (QPalette.ColorRole.Base, "editor"),
        (QPalette.ColorRole.AlternateBase, "surface"),
        (QPalette.ColorRole.Text, "text"),
        (QPalette.ColorRole.Button, "bg"),
        (QPalette.ColorRole.ButtonText, "text"),
        (QPalette.ColorRole.Highlight, "accent-soft"),
        (QPalette.ColorRole.HighlightedText, "text"),
        (QPalette.ColorRole.ToolTipBase, "raised"),
        (QPalette.ColorRole.ToolTipText, "text"),
        (QPalette.ColorRole.PlaceholderText, "dim"),
        (QPalette.ColorRole.Light, "border"),
        (QPalette.ColorRole.Midlight, "raised"),
        (QPalette.ColorRole.Dark, "border-soft"),
        (QPalette.ColorRole.Mid, "border"),
        (QPalette.ColorRole.Shadow, "border-soft"),
    ):
        palette.setColor(role, QColor(t[key]))
    palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.ButtonText, QColor(t["dim"]))
    palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, QColor(t["dim"]))
    return palette


def stylesheet(t: dict[str, str]) -> str:
    """Genera QSS compartido por las dos paletas, sin cambiar tamaños del código.

    Los selectores #nombre corresponden a setObjectName en los componentes.
    Los selectores [role=…] corresponden a setProperty y reutilizan un color
    semántico sin asignar estilos sueltos a cada etiqueta.
    """
    return f"""
    QWidget {{ color: {t['text']}; }}
    QMainWindow, QDialog, QWidget#workspace {{ background: {t['bg']}; }}
    QFrame#topbar, QFrame#statusbar {{ background: {t['sidebar']}; }}
    QFrame#topbar {{ border-bottom: 1px solid {t['border']}; }}
    QFrame#statusbar {{ border-top: 1px solid {t['border']}; }}
    QWidget#explorer {{ background: {t['sidebar']}; }}
    QFrame#appearancePanel {{ background: {t['bg']}; border-left: 1px solid {t['border']}; }}
    QFrame#editorHeading {{ background: {t['editor']}; border-bottom: 1px solid {t['border-soft']}; }}
    QLabel[role="muted"] {{ color: {t['muted']}; }}
    QLabel[role="eyebrow"] {{ color: {t['muted']}; font-size: 10px; letter-spacing: 1px; }}
    QLabel[role="accent"] {{ color: {t['accent']}; }}
    QLabel[role="error"] {{ color: {t['error']}; }}
    QLabel#brand {{ font-size: 17px; font-weight: bold; }}
    QLabel#activeFilename {{ font-size: 15px; font-weight: bold; }}
    QLabel#sampleBadge {{ color: {t['muted']}; font-size: 10px; }}
    QLabel#fileRow {{ padding: 10px 8px; color: {t['muted']}; }}
    QLabel#fileRow[active="true"] {{ background: {t['accent-soft']}; color: {t['text']}; border: 1px solid {t['border']}; }}
    QFrame#divider {{ background: {t['border-soft']}; max-height: 1px; }}
    QPushButton, QToolButton {{ background: transparent; border: 1px solid transparent; padding: 7px 9px; border-radius: 2px; }}
    QPushButton:hover, QToolButton:hover {{ background: {t['raised']}; border-color: {t['border']}; }}
    QPushButton:pressed, QToolButton:pressed {{ background: {t['accent-soft']}; }}
    QPushButton:focus, QToolButton:focus {{ border: 1px solid {t['accent']}; }}
    QPushButton:disabled, QToolButton:disabled {{ color: {t['dim']}; }}
    QPushButton#compileButton {{ color: {t['accent']}; border: 1px solid {t['accent']}; padding: 8px 14px; }}
    QPushButton#compileButton:disabled {{ color: {t['dim']}; border-color: {t['border']}; }}
    QPushButton#themeChoice {{ text-align: left; border: 1px solid {t['border']}; padding: 13px; }}
    QPushButton#themeChoice:checked {{ border-color: {t['accent']}; background: {t['accent-soft']}; }}
    QPushButton#resetAppearance {{ border: 1px solid {t['border']}; }}
    QComboBox {{ background: {t['sidebar']}; border: 1px solid {t['border']}; border-radius: 2px; padding: 7px 8px; min-width: 150px; }}
    QComboBox:hover {{ border-color: {t['accent']}; }}
    QComboBox QAbstractItemView {{ background: {t['surface']}; color: {t['text']}; selection-background-color: {t['accent-soft']}; border: 1px solid {t['border']}; }}
    QMenu {{ background: {t['surface']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 5px; }}
    QMenu::item {{ padding: 8px 24px; }}
    QMenu::item:selected {{ background: {t['accent-soft']}; }}
    QMenu::item:disabled {{ color: {t['dim']}; }}
    QMenu::separator {{ height: 1px; background: {t['border']}; margin: 4px; }}
    QPlainTextEdit {{ background: {t['editor']}; color: {t['text']}; border: 0; selection-background-color: {t['accent-soft']}; selection-color: {t['text']}; padding: 8px; }}
    QPlainTextEdit#console {{ background: {t['sidebar']}; padding: 4px 0; }}
    QTabWidget::pane {{ border: 0; border-top: 1px solid {t['border-soft']}; background: {t['bg']}; }}
    QTabBar::tab {{ background: {t['sidebar']}; color: {t['muted']}; padding: 12px 18px; border-top: 1px solid transparent; }}
    QTabBar::tab:selected {{ color: {t['accent']}; background: {t['surface']}; border-top-color: {t['accent']}; }}
    QTabBar::tab:hover {{ background: {t['raised']}; }}
    QWidget#outputPage {{ background: {t['bg']}; }}
    QSplitter::handle {{ background: {t['border']}; }}
    QSplitter::handle:hover {{ background: {t['accent']}; }}
    QScrollBar:vertical {{ background: {t['editor']}; width: 10px; margin: 0; }}
    QScrollBar:horizontal {{ background: {t['editor']}; height: 10px; margin: 0; }}
    QScrollBar::handle {{ background: {t['border']}; border-radius: 3px; min-width: 24px; min-height: 24px; }}
    QScrollBar::handle:hover {{ background: {t['dim']}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
    QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}
    QSlider::groove:horizontal {{ height: 4px; background: {t['border']}; }}
    QSlider::sub-page:horizontal {{ background: {t['accent']}; }}
    QSlider::handle:horizontal {{ width: 14px; margin: -5px 0; background: {t['accent']}; border-radius: 7px; }}
    QToolTip {{ background: {t['raised']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 6px; }}
    """
