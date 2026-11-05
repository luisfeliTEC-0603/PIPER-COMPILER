"""Arranque de Piper IDE con el intérprete de Python elegido por el usuario.

``python -m piper_ide``, la ejecución directa y el ejecutable instalado llaman
a ``main``. El intérprete debe tener instaladas las dependencias del proyecto;
no se cambia de Python ni se instala software automáticamente.
"""

from pathlib import Path
import sys


def main() -> int:
    """Crea la aplicación Qt, muestra la ventana y devuelve su código de salida.

    Qt necesita una QApplication antes de construir widgets, fuentes o iconos.
    La variable ``window`` conserva la ventana durante todo el bucle de eventos.
    Este arranque no invoca al compilador ni abre archivos del proyecto.
    """
    try:
        from PySide6.QtWidgets import QApplication
    except ModuleNotFoundError as error:
        if error.name != "PySide6":
            raise
        raise SystemExit(
            "Falta PySide6 en este Python. Instalá las dependencias con "
            "python -m pip install -e ide desde la raíz del proyecto."
        ) from None

    from piper_ide.ui.main_window import MainWindow
    from piper_ide.ui.theme import monospace_font

    app = QApplication(sys.argv)
    app.setApplicationName("Piper IDE")
    app.setOrganizationName("Piper")
    app.setStyle("Fusion")
    app.setFont(monospace_font(12))
    window = MainWindow()
    window.show()
    return app.exec()


def _run_from_source() -> int:
    """Prepara la ejecución directa de app.py sin depender del directorio actual.

    Añade ``ide/`` a la ruta de imports para resolver ``piper_ide`` incluso sin
    una instalación editable. Mantiene el intérprete que inició el archivo,
    tanto si pertenece a un entorno virtual como a una instalación habitual.
    """
    ide_directory = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(ide_directory))
    return main()


if __name__ == "__main__":
    raise SystemExit(_run_from_source())
