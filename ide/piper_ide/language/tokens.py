"""Conversión de terminales LL(1) en opciones insertables por el editor."""

from dataclasses import dataclass
import re


KEYWORDS = frozenset({
    "create", "routine", "loop", "alwhen", "when", "fallback", "store",
    "bring", "from", "break", "output", "none", "true", "false", "bool",
    "B8", "uB8", "B16", "uB16", "B32", "uB32", "and", "or", "xor",
})
_IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
_IGNORED_TERMINALS = frozenset({"$", "token inválido", "ruta .rwt"})


@dataclass(frozen=True, slots=True)
class CompletionItem:
    """Opción insertable acompañada por ayuda puramente visual."""

    text: str
    signature: str = ""
    description: str = ""

    @property
    def detail(self) -> str:
        """Combina la forma de uso y la explicación para la segunda línea."""
        if self.signature and self.description:
            return f"{self.signature}  ·  {self.description}"
        return self.signature or self.description


# Estas formas siguen la gramática vigente de Piper. Solo documentan la opción
# en el popup: seleccionar una fila continúa insertando exclusivamente ``text``.
_SYNTAX_HELP: dict[str, tuple[str, str]] = {
    "routine": (
        "routine <tipo> <nombre>[<tipo> <parámetro>, ...] { ... }",
        "Declara una rutina.",
    ),
    "create": (
        "create <tipo> [|<dimensión>&...|] <nombre>.",
        "Declara una variable o arreglo.",
    ),
    "loop": (
        "loop[<control> & <inicio> -> <fin> & <paso>] { ... }",
        "Repite un bloque dentro de un rango.",
    ),
    "when": (
        "when[<condición>] { ... } [fallback { ... }]",
        "Ejecuta un bloque de forma condicional.",
    ),
    "alwhen": (
        "alwhen[<condición>] { ... }",
        "Ejecuta un bloque mientras se cumpla la condición.",
    ),
    "fallback": ("fallback { ... }", "Alternativa de un bloque when."),
    "bring": (
        'bring <módulo> from "<archivo>.rwt".',
        "Importa un módulo Piper.",
    ),
    "from": (
        'bring <módulo> from "<archivo>.rwt".',
        "Indica el archivo de origen.",
    ),
    "store": (
        "<destino> store <expresión>.",
        "Guarda un valor en una variable o posición.",
    ),
    "output": ("output <expresión>.", "Envía un valor a la salida."),
    "break": ("break.", "Finaliza el loop actual."),
    "bool": ("bool", "Tipo lógico."),
    "B8": ("B8", "Entero con signo de 8 bits."),
    "uB8": ("uB8", "Entero sin signo de 8 bits."),
    "B16": ("B16", "Entero con signo de 16 bits."),
    "uB16": ("uB16", "Entero sin signo de 16 bits."),
    "B32": ("B32", "Entero con signo de 32 bits."),
    "uB32": ("uB32", "Entero sin signo de 32 bits."),
    "true": ("true", "Literal lógico verdadero."),
    "false": ("false", "Literal lógico falso."),
    "and": ("<izquierda> and <derecha>", "Conjunción lógica."),
    "or": ("<izquierda> or <derecha>", "Disyunción lógica."),
    "xor": ("<izquierda> xor <derecha>", "Disyunción exclusiva."),
    "->": ("<inicio> -> <fin>", "Separa los extremos de un loop."),
    ".": ("<instrucción>.", "Finaliza una instrucción simple."),
}


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


def completion_items(expected: tuple[str, ...], fragment: str,
                     source: str) -> tuple[CompletionItem, ...]:
    """Añade firmas y descripciones sin alterar la predicción ni la inserción.

    Los identificadores continúan proviniendo del documento y los terminales
    continúan filtrándose con el resultado LL(1). La información adicional es
    estática porque describe la sintaxis del lenguaje, no el estado del parser.
    """
    identifiers = frozenset(document_identifiers(source))
    items: list[CompletionItem] = []
    for candidate in completion_candidates(expected, fragment, source):
        signature, description = _SYNTAX_HELP.get(candidate, ("", ""))
        if candidate in identifiers:
            description = "Identificador presente en el documento."
        elif candidate == "0":
            description = "Literal entero."
        items.append(CompletionItem(candidate, signature, description))
    return tuple(items)
