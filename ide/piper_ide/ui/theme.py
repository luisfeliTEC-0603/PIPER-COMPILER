"""Recursos y estilo común para los modos claro y oscuro del IDE.

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
    for family in (
        "Monaco", "Menlo", "SF Mono", "JetBrains Mono", "Iosevka",
        "Cascadia Mono", "IBM Plex Mono", "Consolas", "DejaVu Sans Mono",
    ):
        if family in available:
            font.setFamily(family)
            break
    font.setFixedPitch(True)
    font.setKerning(False)
    font.setStyleHint(QFont.StyleHint.Monospace)
    font.setHintingPreference(QFont.HintingPreference.PreferFullHinting)
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
        (QPalette.ColorRole.HighlightedText, "accent-strong"),
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
    family = monospace_font(12).family().replace('"', '\\"')
    return f"""
    * {{ outline: 0; font-family: "{family}"; }}
    QWidget {{ color: {t['text']}; }}
    QMainWindow, QDialog, QWidget#workspace {{ background: {t['bg']}; }}
    QFrame#topbar {{ background: {t['sidebar']}; border-bottom: 1px solid {t['border']}; }}
    QFrame#statusbar {{ background: {t['surface']}; border-top: 1px solid {t['border']}; }}
    QWidget#explorer {{ background: {t['sidebar']}; border-right: 1px solid {t['border']}; }}
    QLabel[role="muted"] {{ color: {t['muted']}; }}
    QLabel[role="eyebrow"] {{ color: {t['muted']}; font-size: 10px; }}
    QLabel[role="accent"] {{ color: {t['accent']}; }}
    QLabel[role="error"] {{ color: {t['error']}; }}
    QLabel#brandMascot {{ padding-left: 8px; padding-right: 3px; }}
    QLabel#brand {{ color: {t['accent']}; font-size: 12px; font-weight: 700; padding: 0 8px; }}
    QLabel#sectionHeading {{ background: transparent; color: {t['muted']}; border-top: 1px solid {t['border-soft']}; border-bottom: 1px solid {t['border-soft']}; padding: 3px 8px; font-size: 10px; font-weight: 700; }}
    QLabel#activeFilename {{ font-size: 12px; font-weight: 700; }}
    QLabel#pathLabel {{ color: {t['dim']}; }}
    QLabel#kindBadge {{ color: {t['muted']}; padding: 1px 0; }}
    QLabel#statusBrand {{ background: {t['surface']}; color: {t['text']}; border-right: 1px solid {t['border']}; font-weight: 700; padding: 1px 10px; }}
    QLabel#modifiedBadge {{ color: {t['error']}; }}
    QListWidget, QTreeWidget {{ background: {t['sidebar']}; color: {t['muted']}; border: 0; outline: 0; }}
    QListWidget::item {{ padding: 3px 8px; }}
    QTreeWidget::item {{ padding: 2px 7px; }}
    QListWidget::item:selected, QTreeWidget::item:selected {{ background: {t['accent-soft']}; color: {t['accent-strong']}; }}
    QListWidget::item:hover, QTreeWidget::item:hover {{ background: {t['raised']}; color: {t['text']}; }}
    QFrame#searchBar {{ background: {t['surface']}; border-bottom: 1px solid {t['border']}; }}
    QLineEdit {{ background: {t['editor']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 5px; selection-background-color: {t['accent-soft']}; }}
    QLineEdit:focus {{ border-color: {t['accent']}; }}
    QFrame#appearancePanel {{ background: {t['bg']}; border-left: 1px solid {t['border']}; }}
    QFrame#editorHeading {{ background: {t['surface']}; border-bottom: 1px solid {t['border']}; }}
    QPushButton, QToolButton {{ background: transparent; border: 1px solid transparent; padding: 5px 8px; border-radius: 0; }}
    QPushButton:hover, QToolButton:hover {{ background: {t['raised']}; border-color: {t['border-soft']}; }}
    QPushButton:pressed, QToolButton:pressed {{ background: {t['accent-soft']}; color: {t['accent-strong']}; }}
    QPushButton:focus, QToolButton:focus {{ border-color: {t['accent']}; }}
    QPushButton:disabled, QToolButton:disabled {{ color: {t['dim']}; }}
    QPushButton#menuButton {{ color: {t['muted']}; padding: 4px 8px; border-left: 1px solid transparent; border-right: 1px solid transparent; }}
    QPushButton#menuButton:hover {{ color: {t['text']}; background: {t['raised']}; border-left-color: {t['border']}; border-right-color: {t['border']}; }}
    QPushButton#openFolderButton {{ color: {t['accent']}; text-align: left; padding: 6px 9px; border-top: 1px solid {t['border-soft']}; border-bottom: 1px solid {t['border-soft']}; }}
    QPushButton#compileButton {{ color: {t['accent']}; border-left: 1px solid {t['border']}; padding: 4px 10px; font-weight: 700; }}
    QPushButton#compileButton:disabled {{ color: {t['dim']}; border-color: {t['border']}; }}
    QPushButton#saveButton {{ color: {t['muted']}; border-left: 1px solid {t['border']}; padding: 4px 9px; }}
    QPushButton#themeChoice {{ text-align: left; border: 0; border-bottom: 1px solid {t['border-soft']}; padding: 9px 4px; }}
    QPushButton#themeChoice:checked {{ color: {t['accent-strong']}; border-bottom-color: {t['accent']}; background: {t['accent-soft']}; }}
    QPushButton#resetAppearance {{ border: 1px solid {t['border']}; }}
    QToolButton:checked {{ background: {t['accent-soft']}; color: {t['accent-strong']}; border-color: {t['border']}; }}
    QComboBox {{ background: {t['sidebar']}; border: 1px solid {t['border']}; border-radius: 0; padding: 4px 7px; min-width: 128px; }}
    QComboBox:hover {{ border-color: {t['accent']}; }}
    QComboBox QAbstractItemView {{ background: {t['surface']}; color: {t['text']}; selection-background-color: {t['accent-soft']}; border: 1px solid {t['border']}; }}
    QAbstractItemView#completionPopup {{ background: {t['surface']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 2px; }}
    QAbstractItemView#completionPopup::item {{ border-bottom: 1px solid {t['border-soft']}; }}
    QAbstractItemView#completionPopup::item:hover {{ background: {t['raised']}; }}
    QAbstractItemView#completionPopup::item:selected {{ background: {t['accent-soft']}; color: {t['accent-strong']}; }}
    QMenu {{ background: {t['surface']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 3px; }}
    QMenu::item {{ padding: 6px 24px; }}
    QMenu::item:selected {{ background: {t['accent-soft']}; color: {t['accent-strong']}; }}
    QMenu::item:disabled {{ color: {t['dim']}; }}
    QMenu::separator {{ height: 1px; background: {t['border']}; margin: 3px; }}
    QPlainTextEdit {{ background: {t['editor']}; color: {t['text']}; border: 0; selection-background-color: {t['accent-soft']}; selection-color: {t['text']}; padding: 6px; }}
    QPlainTextEdit#console {{ background: {t['editor']}; color: {t['muted']}; padding: 5px 8px; }}
    QTabWidget::pane {{ border: 0; border-top: 1px solid {t['border']}; background: {t['editor']}; }}
    QTabBar::tab {{ background: {t['surface']}; color: {t['muted']}; padding: 4px 12px; border-right: 1px solid {t['border-soft']}; border-bottom: 1px solid transparent; }}
    QTabBar::tab:selected {{ color: {t['accent-strong']}; background: {t['editor']}; border-bottom-color: {t['accent']}; }}
    QTabBar::tab:hover {{ background: {t['raised']}; color: {t['text']}; }}
    QWidget#outputPage {{ background: {t['editor']}; }}
    QSplitter::handle {{ background: {t['border']}; }}
    QSplitter::handle:hover {{ background: {t['accent']}; }}
    QScrollBar:vertical {{ background: {t['editor']}; width: 8px; margin: 0; }}
    QScrollBar:horizontal {{ background: {t['editor']}; height: 8px; margin: 0; }}
    QScrollBar::handle {{ background: {t['border']}; min-width: 22px; min-height: 22px; }}
    QScrollBar::handle:hover {{ background: {t['dim']}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
    QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}
    QSlider::groove:horizontal {{ height: 3px; background: {t['border']}; }}
    QSlider::sub-page:horizontal {{ background: {t['accent']}; }}
    QSlider::handle:horizontal {{ width: 12px; margin: -5px 0; background: {t['accent']}; border-radius: 0; }}
    QToolTip {{ background: {t['raised']}; color: {t['text']}; border: 1px solid {t['border']}; padding: 5px; }}
    """
