"""Lectura de texto y guardado atómico; esta capa no abre diálogos.

Se admite UTF-8, UTF-8 con BOM y UTF-16 con BOM. Los binarios se rechazan antes
de llegar al editor. Guardar compara una huella del disco y usa QSaveFile:
un fallo de escritura no reemplaza el archivo original con contenido parcial.
"""

import codecs
from dataclasses import dataclass
import hashlib
from pathlib import Path

from PySide6.QtCore import QIODevice, QSaveFile


MAX_TEXT_BYTES = 16 * 1024 * 1024
BINARY_SUFFIXES = {".bin", ".exe", ".dll", ".png", ".jpg", ".jpeg", ".gif",
                   ".pdf", ".zip", ".pyc", ".o", ".woff", ".ttf", ".ico"}


class TextFileError(ValueError):
    """Archivo binario, demasiado grande o con una codificación no admitida."""


class DiskChangedError(OSError):
    """El archivo cambió o desapareció desde su última lectura en el IDE."""


@dataclass(frozen=True)
class FileContent:
    """Texto normalizado para Qt y datos necesarios para reconstruir sus bytes."""

    text: str
    encoding: str
    newline: str
    digest: str


def disk_digest(path: Path) -> str | None:
    """Calcula SHA-256 por bloques; devuelve None si el archivo no existe."""
    try:
        with path.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    except FileNotFoundError:
        return None


def read_text_file(path: Path) -> FileContent:
    """Lee un archivo textual sin sustituir caracteres que no pueda decodificar.

    El límite de 16 MiB evita bloquear la ventana al abrir accidentalmente un
    archivo enorme. Los finales CRLF/CR se normalizan en memoria y se conserva
    el primero encontrado como formato de escritura. No se agrega una línea final.
    """
    if path.suffix.lower() in BINARY_SUFFIXES:
        raise TextFileError("Este archivo es binario. Abrí su ASM, bundles o memoria para consultar el código generado.")
    with path.open("rb") as source:
        raw = source.read(MAX_TEXT_BYTES + 1)
    if len(raw) > MAX_TEXT_BYTES:
        raise TextFileError("El editor admite archivos de texto de hasta 16 MiB.")
    if raw.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
        encoding = "utf-16-le" if raw.startswith(codecs.BOM_UTF16_LE) else "utf-16-be"
        payload = raw[2:]
    else:
        encoding = "utf-8-sig" if raw.startswith(codecs.BOM_UTF8) else "utf-8"
        payload = raw
        if b"\0" in raw:
            raise TextFileError("El contenido parece binario y no se puede abrir como texto.")
    try:
        text = payload.decode(encoding)
    except UnicodeDecodeError as error:
        raise TextFileError("No se pudo leer como UTF-8 o UTF-16 con BOM; el archivo no fue modificado.") from error
    if any(ord(char) < 32 and char not in "\t\r\n\f" for char in text):
        raise TextFileError("El contenido contiene controles binarios y no se puede editar como texto.")
    first_break = next((index for index, char in enumerate(text) if char in "\r\n"), None)
    newline = "\n" if first_break is None else (
        "\r\n" if text[first_break:first_break + 2] == "\r\n" else text[first_break]
    )
    return FileContent(text.replace("\r\n", "\n").replace("\r", "\n"), encoding,
                       newline, hashlib.sha256(raw).hexdigest())


def write_text_file(path: Path, text: str, encoding: str, newline: str,
                    expected_digest: str | None, *, force: bool = False) -> str:
    """Guarda y devuelve la nueva huella; no cambia metadatos del documento.

    expected_digest=None exige un destino nuevo. Una diferencia en disco requiere
    que la interfaz pida confirmación antes de repetir con force=True. QSaveFile
    escribe junto al destino y commit lo sustituye al finalizar correctamente.
    Se mantiene desactivado el fallback que escribiría directamente sobre él.
    """
    if path.suffix.lower() in BINARY_SUFFIXES:
        raise TextFileError("El destino tiene una extensión binaria. Elegí un archivo de texto.")
    if not force and disk_digest(path) != expected_digest:
        raise DiskChangedError("El archivo cambió fuera del IDE o fue eliminado.")
    payload = text.replace("\n", newline).encode(encoding)
    if encoding in ("utf-16-le", "utf-16-be"):
        payload = (codecs.BOM_UTF16_LE if encoding == "utf-16-le" else codecs.BOM_UTF16_BE) + payload
    target = QSaveFile(str(path))
    target.setDirectWriteFallback(False)
    if not target.open(QIODevice.OpenModeFlag.WriteOnly):
        raise OSError(target.errorString())
    if target.write(payload) != len(payload):
        target.cancelWriting()
        raise OSError(target.errorString())
    if not target.commit():
        raise OSError(target.errorString())
    return hashlib.sha256(payload).hexdigest()
