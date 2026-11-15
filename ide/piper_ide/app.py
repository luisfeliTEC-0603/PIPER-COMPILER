"""Arranque de Piper IDE con el intérprete de Python elegido por el usuario.

``python -m piper_ide``, la ejecución directa y el ejecutable instalado llaman
a ``main``. El intérprete debe tener instaladas las dependencias del proyecto;
no se cambia de Python ni se instala software automáticamente.
"""

import os
from pathlib import Path
import stat
import sys


def _prepare_macos_qt_plugins(pyside_package) -> Path | None:
    """Hace visible y registra el backend Cocoa antes de crear QApplication.

    Algunos entornos virtuales bajo una carpeta que empieza con punto heredan
    ``UF_HIDDEN`` en los plugins de Qt. Python y Qt omiten esos archivos aunque
    existan. La reparación es local, idempotente y se repite en cada arranque.
    """
    if sys.platform != "darwin":
        return None
    plugin_root = Path(pyside_package.__file__).resolve().parent / "Qt" / "plugins"
    platforms = plugin_root / "platforms"
    hidden = getattr(stat, "UF_HIDDEN", 0)
    if hidden and platforms.is_dir():
        for target in (plugin_root, platforms, *platforms.glob("*.dylib")):
            try:
                flags = target.stat().st_flags
                if flags & hidden:
                    os.chflags(target, flags & ~hidden)
            except OSError:
                # Qt todavía puede cargar el plugin si el sistema permite leerlo;
                # el diagnóstico nativo explicará cualquier fallo restante.
                pass
    if platforms.is_dir():
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(platforms)
        return plugin_root
    return None


def main() -> int:
    """Crea la aplicación Qt, muestra la ventana y devuelve su código de salida.

    Qt necesita una QApplication antes de construir widgets, fuentes o iconos.
    La variable ``window`` conserva la ventana durante todo el bucle de eventos.
    Este arranque no invoca al compilador ni abre archivos del proyecto.
    """
    try:
        import PySide6
        from PySide6.QtCore import QCoreApplication
        from PySide6.QtWidgets import QApplication
    except ModuleNotFoundError as error:
        if error.name != "PySide6":
            raise
        raise SystemExit(
            "Falta PySide6 en este Python. Instalá las dependencias con "
            "python -m pip install ./ide desde la raíz del proyecto."
        ) from None

    plugin_root = _prepare_macos_qt_plugins(PySide6)
    if plugin_root is not None:
        QCoreApplication.addLibraryPath(str(plugin_root))

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
