"""Contexto textual mínimo que el puente LL(1) necesita en el cursor."""

from dataclasses import dataclass
import re


_FRAGMENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*$")


@dataclass(frozen=True, slots=True)
class CompletionContext:
    """Prefijo analizable y fragmento que será reemplazado al completar."""

    parse_prefix: str
    fragment: str
    suppressed: bool = False


def completion_context(text_before_cursor: str) -> CompletionContext:
    """Separa la palabra parcial sin enviarla al parser LL(1).

    El lexer interpretaría ``cre`` como identificador. Al retirarlo, Pipoc
    consulta el estado anterior y el IDE puede filtrar ``create`` con ese
    fragmento. Dentro de comentarios o una ruta entre comillas no se abre el
    popup, porque esas regiones no representan una posición sintáctica.
    """
    line = text_before_cursor.rsplit("\n", 1)[-1]
    in_block_comment = text_before_cursor.count("***") % 2 == 1
    in_line_comment = "~~" in line and not in_block_comment
    in_quoted_path = line.count('"') % 2 == 1
    match = _FRAGMENT.search(text_before_cursor)
    fragment = match.group(0) if match else ""
    parse_prefix = text_before_cursor[:match.start()] if match else text_before_cursor
    return CompletionContext(
        parse_prefix, fragment,
        in_block_comment or in_line_comment or in_quoted_path,
    )
