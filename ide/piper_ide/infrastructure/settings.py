"""Preferencias de compilación; sin guardar rutas ni contenido del programa."""

from PySide6.QtCore import QSettings

from ..models.compilation import OptimizationOptions


def _settings() -> QSettings:
    # Respetar el formato configurado por la aplicación (también permite
    # comprobar preferencias en una carpeta temporal sin tocar las reales).
    return QSettings(QSettings.defaultFormat(), QSettings.Scope.UserScope, "Piper", "Piper IDE")


def load_optimizations() -> OptimizationOptions:
    settings = _settings()
    # Datos inválidos o de otra versión vuelven a los valores iniciales.
    try:
        return OptimizationOptions(
            unroll=settings.value("compilation/unroll", False, type=bool),
            dce=settings.value("compilation/dce", False, type=bool),
            rename=settings.value("compilation/rename", False, type=bool),
            allow_spill=settings.value("compilation/spill", True, type=bool),
        )
    except (ValueError, TypeError):
        return OptimizationOptions()


def save_optimizations(options: OptimizationOptions) -> None:
    settings = _settings()
    # Descartar preferencias del antiguo selector manual al guardar.
    settings.remove("compilation/useFactor")
    settings.remove("compilation/factor")
    for key, value in (("unroll", options.unroll), ("dce", options.dce),
                       ("rename", options.rename), ("spill", options.allow_spill)):
        settings.setValue("compilation/" + key, value)


def simulator_folder() -> str:
    return _settings().value("metrics/simulator", "", type=str)


def save_simulator_folder(folder: str) -> None:
    _settings().setValue("metrics/simulator", folder)
