"""Buffers abiertos: un documento Qt por archivo conserva texto y Deshacer.

La ventana intercambia el QTextDocument del editor en vez de volver a cargar
cadenas. El padre de cada buffer es el workspace, nunca el editor: Qt puede
destruir un documento hijo del editor al sustituirlo con setDocument.
"""

from dataclasses import dataclass

from PySide6.QtGui import QTextDocument
from PySide6.QtWidgets import QPlainTextDocumentLayout

from ..editor.highlighter import CodeHighlighter
from ..models.document import Document
from .theme import monospace_font


@dataclass
class EditorSession:
    """Metadatos, buffer, resaltador y posición de un documento abierto."""

    info: Document
    buffer: QTextDocument
    highlighter: CodeHighlighter
    position: int = 0
    anchor: int = 0
    vertical_scroll: int = 0
    horizontal_scroll: int = 0


def create_session(info: Document, text: str, colors: dict[str, str], size: int,
                   parent) -> EditorSession:
    """Construye un buffer compatible con QPlainTextEdit, inicialmente sin cambios.

    QPlainTextDocumentLayout es obligatorio al compartir documentos con ese
    widget. El resaltado se conserva aunque el archivo no esté visible.
    """
    buffer = QTextDocument(parent)
    buffer.setDocumentLayout(QPlainTextDocumentLayout(buffer))
    buffer.setDefaultFont(monospace_font(size))
    buffer.setPlainText(text)
    buffer.setModified(False)
    highlighter = CodeHighlighter(buffer, info.language, colors)
    return EditorSession(info, buffer, highlighter)
