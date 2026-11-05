"""Conversión de terminales LL(1) en opciones insertables por el editor."""

import re


KEYWORDS = frozenset({
    "create", "routine", "loop", "alwhen", "when", "fallback", "store",
    "bring", "from", "break", "output", "none", "true", "false", "bool",
    "B8", "uB8", "B16", "uB16", "B32", "uB32", "and", "or", "xor",
})
_IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
_IGNORED_TERMINALS = frozenset({"$", "token inválido", "ruta .rwt"})


def document_identifiers(source: str) -> tuple[str, ...]:
    """Obtiene nombres escritos en el documento sin pretender resolver scopes."""
    return tuple(sorted({
        match.group(0) for match in _IDENTIFIER.finditer(source)
        if match.group(0) not in KEYWORDS
    }))


def completion_candidates(expected: tuple[str, ...], fragment: str,
                          source: str) -> tuple[str, ...]:
    """Expande terminales abstractos y filtra por el fragmento del usuario.

    ``identificador`` se materializa con nombres ya presentes en el buffer y
    ``entero`` ofrece un literal neutral. El filtrado respeta mayúsculas porque
    el lexer de Piper también las distingue.
    """
    candidates: list[str] = []
    identifiers = document_identifiers(source)
    for terminal in expected:
        if terminal in _IGNORED_TERMINALS:
            continue
        if terminal == "identificador":
            candidates.extend(name for name in identifiers if name != fragment)
        elif terminal == "entero":
            candidates.append("0")
        else:
            candidates.append(terminal)
    unique = dict.fromkeys(
        candidate for candidate in candidates
        if not fragment or candidate.startswith(fragment)
    )
    return tuple(unique)
