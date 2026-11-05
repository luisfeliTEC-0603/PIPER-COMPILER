"""Colores de sintaxis para texto; no produce diagnósticos ni cambia el código.

Se recorre cada bloque en orden para que una palabra dentro de un comentario
o una cadena nunca se pinte como código. El estado entre bloques solo se usa
para comentarios *** de Piper. Qt mantiene este formato fuera del texto real.
"""

import re

from PySide6.QtGui import QColor, QSyntaxHighlighter, QTextCharFormat

from ..language.piper import CONSTANTS, KEYWORDS, TYPES
from ..language.porygon_asm import MNEMONICS


_PIPER = re.compile(r'(?P<block>\*\*\*)|(?P<comment>~~.*)|(?P<string>"(?:\\.|[^"\\])*"?)|(?P<number>\b\d+\b)|(?P<word>\b[A-Za-z_][A-Za-z0-9_]*\b)')
_ASM = re.compile(r'(?P<comment>;.*|//.*)|(?P<string>"(?:\\.|[^"\\])*"?)|(?P<number>\#?-?(?:0[xX][0-9a-fA-F]+|\b\d+\b))|(?P<word>\b[A-Za-z_][A-Za-z0-9_]*\b)')
_JSON = re.compile(r'(?P<string>"(?:\\.|[^"\\])*")|(?P<number>-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|(?P<word>\b(?:true|false|null)\b)')


class CodeHighlighter(QSyntaxHighlighter):
    """Resaltador asociado a un único QTextDocument durante toda su vida."""

    def __init__(self, document, language: str, colors: dict[str, str]):
        """Asocia documento, formato y colores; MainWindow conserva su instancia."""
        super().__init__(document)
        self.language = language
        self.formats = {}
        self.apply_theme(colors)

    def apply_theme(self, colors: dict[str, str]):
        """Reconstruye formatos y repinta sin modificar el historial del documento."""
        self.formats = {}
        for role in ("keyword", "type", "number", "function", "comment"):
            char_format = QTextCharFormat()
            char_format.setForeground(QColor(colors[role]))
            self.formats[role] = char_format
        self.rehighlight()

    def _paint(self, text: str, start: int, end: int, role: str):
        """Traduce índices Python a unidades UTF-16, que son las coordenadas de Qt.

        Un emoji ocupa un carácter Python y dos unidades Qt. Convertir evita
        desplazar los colores del resto de la línea cuando hay texto Unicode.
        """
        qt_start = start if text.isascii() else len(text[:start].encode("utf-16-le")) // 2
        qt_count = end - start if text.isascii() else len(text[start:end].encode("utf-16-le")) // 2
        self.setFormat(qt_start, qt_count, self.formats[role])

    def highlightBlock(self, text: str):
        """Colorea el bloque actual respetando cadenas y comentarios completos."""
        self.setCurrentBlockState(0)
        if not self.formats or self.language == "text":
            return
        if self.language == "memory":
            for match in re.finditer(r"\b[0-9a-fA-F]+\b", text):
                self._paint(text, match.start(), match.end(), "number")
            return
        position = 0
        if self.language == "piper" and self.previousBlockState() == 1:
            end = text.find("***")
            if end < 0:
                self._paint(text, 0, len(text), "comment")
                self.setCurrentBlockState(1)
                return
            position = end + 3
            self._paint(text, 0, position, "comment")
        pattern = {"piper": _PIPER, "asm": _ASM, "json": _JSON}[self.language]
        while (match := pattern.search(text, position)) is not None:
            position = match.end()
            kind = match.lastgroup
            value = match.group()
            role = None
            if kind == "block":
                end = text.find("***", position)
                position = len(text) if end < 0 else end + 3
                self._paint(text, match.start(), position, "comment")
                if end < 0:
                    self.setCurrentBlockState(1)
            elif kind == "comment":
                role = "comment"
            elif kind == "string":
                role = "type" if self.language != "json" or not text[position:].lstrip().startswith(":") else "keyword"
            elif kind == "number":
                role = "number"
            elif self.language == "piper":
                if value in KEYWORDS:
                    role = "keyword"
                elif value in TYPES:
                    role = "type"
                elif value in CONSTANTS:
                    role = "number"
                elif text[position:].lstrip().startswith("["):
                    role = "function"
            elif self.language == "asm":
                if value.lower() in MNEMONICS:
                    role = "keyword"
                elif re.fullmatch(r"r[0-7]", value, re.IGNORECASE):
                    role = "type"
                elif text[position:].lstrip().startswith(":"):
                    role = "function"
            else:
                role = "number"
            if role:
                self._paint(text, match.start(), match.end(), role)
