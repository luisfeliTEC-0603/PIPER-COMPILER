from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterator, Optional

try:  # el import es opcional para permitir tests sin PySide6
    from ..models.diagnostic import Diagnostic  # noqa: F401
except Exception:  # pragma: no cover - entorno sin Qt
    Diagnostic = object  # type: ignore


# ---------------------------------------------------------------------------
# Modelo de una corrección
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class QuickFix:
    """Describe una corrección de texto propuesta.

    Atributos:
        title: Texto mostrado en el bombillo / menú contextual.
        line: Línea (base 0) donde se aplica la corrección.
        column: Columna (base 0, unidades de carácter) donde se aplica.
        replacement: Texto que reemplaza el rango ``[start, end)``.
        end_line / end_column: fin exclusivo del rango a reemplazar. Si son
            ``None``, la corrección es una inserción en ``(line, column)``.
        is_snippet: si ``True``, ``replacement`` contiene el marcador ``$0``
            que indica dónde debe quedar el cursor después de aplicar.
        unsafe: si ``True``, la corrección es informativa y no debe aplicarse
            en lote. El título igual lleva el prefijo ``[⚠]``.
    """

    title: str
    line: int
    column: int
    replacement: str
    is_snippet: bool = False
    end_line: Optional[int] = None
    end_column: Optional[int] = None
    unsafe: bool = False


# ---------------------------------------------------------------------------
# Escáner de pares y comentarios
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class _Pair:
    """Par de delimitadores abiertos pendientes de cierre."""

    opening: str
    line: int
    column: int


_OPENERS = {"(": ")", "[": "]", "{": "}"}
_CLOSERS = {")": "(", "]": "[", "}": "{"}


def _scan_pairs(source: str) -> tuple[list[_Pair], list[_Pair]]:
    """Devuelve (pares_abiertos_sin_cerrar, cierres_huérfanos).

    Reglas:
    * ``~~`` inicia un comentario de línea; termina con el salto de línea.
    * ``***`` abre o cierra un comentario de bloque (no anida).
    * ``"..."`` es una cadena con escapes ``\\"``.
    * No se procesan comentarios ni cadenas para el emparejamiento.

    Complejidad O(n). Tolera entrada incompleta sin lanzar excepciones.
    """
    abiertos: list[_Pair] = []
    huerfanos: list[_Pair] = []
    i, n = 0, len(source)
    line, col = 0, 0
    in_line_comment = False
    in_block_comment = False
    in_string = False

    def avanza(caracter: str) -> None:
        nonlocal line, col
        if caracter == "\n":
            line += 1
            col = 0
        else:
            col += 1

    while i < n:
        c = source[i]

        if in_line_comment:
            if c == "\n":
                in_line_comment = False
            avanza(c)
            i += 1
            continue

        if in_block_comment:
            if source.startswith("***", i):
                in_block_comment = False
                avanza("*"); avanza("*"); avanza("*")
                i += 3
                continue
            avanza(c)
            i += 1
            continue

        if in_string:
            if c == "\\" and i + 1 < n:
                avanza(c); avanza(source[i + 1])
                i += 2
                continue
            if c == '"':
                in_string = False
            avanza(c)
            i += 1
            continue

        if source.startswith("~~", i):
            in_line_comment = True
            avanza("~"); avanza("~")
            i += 2
            continue

        if source.startswith("***", i):
            in_block_comment = True
            avanza("*"); avanza("*"); avanza("*")
            i += 3
            continue

        if c == '"':
            in_string = True
            avanza(c)
            i += 1
            continue

        if c in _OPENERS:
            abiertos.append(_Pair(c, line, col))
            avanza(c); i += 1
            continue

        if c in _CLOSERS:
            esperado = _CLOSERS[c]
            while abiertos and abiertos[-1].opening != esperado:
                huerfanos.append(abiertos.pop())
            if abiertos:
                abiertos.pop()
            else:
                huerfanos.append(_Pair(c, line, col))
            avanza(c); i += 1
            continue

        avanza(c)
        i += 1

    return abiertos, huerfanos


def _offset_to_line_col(source: str, offset: int) -> tuple[int, int]:
    """Traduce un offset de Python a (línea, columna) base 0."""
    offset = max(0, min(len(source), offset))
    prefix = source[:offset]
    line = prefix.count("\n")
    column = offset - (prefix.rfind("\n") + 1)
    return line, column


def _end_of_last_token(line_text: str) -> int:
    """Offset (dentro de la línea) tras el último carácter no blanco.

    Si la línea termina en un comentario ``~~ …``, se ignora el comentario:
    el punto de inserción es antes del ``~~``.
    """
    stripped_right = line_text.rstrip()
    marker = stripped_right.find("~~")
    if marker >= 0:
        stripped_right = stripped_right[:marker].rstrip()
    return len(stripped_right)


# ---------------------------------------------------------------------------
# Utilidades sobre el Diagnostic
# ---------------------------------------------------------------------------

def _diagnostic_phase(diagnostic) -> str:
    return getattr(diagnostic, "phase", "") or ""


def _diagnostic_code(diagnostic) -> str:
    return getattr(diagnostic, "code", "") or ""


def _diagnostic_message(diagnostic) -> str:
    return getattr(diagnostic, "message", "") or ""


def _diagnostic_line(diagnostic) -> int:
    return int(getattr(diagnostic, "line", 0) or 0)


def _diagnostic_column(diagnostic) -> int:
    return int(getattr(diagnostic, "column", 0) or 0)


def _diagnostic_end(diagnostic) -> tuple[Optional[int], Optional[int]]:
    return (getattr(diagnostic, "end_line", None),
            getattr(diagnostic, "end_column", None))


# ---------------------------------------------------------------------------
# Fix: falta '.' al final de la instrucción
# ---------------------------------------------------------------------------

def _fix_missing_dot(source: str, diagnostic) -> Iterator[QuickFix]:
    line = _diagnostic_line(diagnostic)
    end_line, end_column = _diagnostic_end(diagnostic)
    lines = source.split("\n")
    if end_line is not None and end_column is not None:
        # El compilador ya reportó un rango vacío en el borde del token previo.
        yield QuickFix(
            title="Añadir '.' al final de la instrucción",
            line=end_line,
            column=end_column,
            replacement=".",
        )
        return
    if line >= len(lines):
        return
    yield QuickFix(
        title="Añadir '.' al final de la instrucción",
        line=line,
        column=_end_of_last_token(lines[line]),
        replacement=".",
    )


# ---------------------------------------------------------------------------
# Fix: cerrar pares pendientes
# ---------------------------------------------------------------------------

# Tokens que, si aparecen al inicio de una línea con indentación menor o
# igual a la del par abierto, indican que el bloque terminó y falta '}'.
_TOP_LEVEL_TOKENS = ("routine", "bring")

def _indentation_of(line_text: str) -> int:
    """Cantidad de espacios iniciales (los tabs cuentan como 4)."""
    count = 0
    for ch in line_text:
        if ch == " ":
            count += 1
        elif ch == "\t":
            count += 4
        else:
            break
    return count

def _find_closure_position(source: str, pair: "_Pair") -> tuple[int, int]:
    """Devuelve (línea, columna) donde cerrar el par abierto ``pair``.

    Recorre las líneas posteriores al ``{`` del par. Si encuentra una línea
    cuyo primer token no-comentario es de nivel superior (``routine``,
    ``bring``) y su indentación es menor o igual a la del par, devuelve
    la posición al inicio de esa línea para insertar el cierre antes de ella.
    Si no encuentra tal línea, devuelve la posición del final del archivo.
    """
    lines = source.split("\n")
    parent_indent = pair.column  # columna de la '{'
    start_line = pair.line + 1
    for index in range(start_line, len(lines)):
        stripped = lines[index].lstrip()
        if not stripped or stripped.startswith("~~"):
            continue
        first_word = re.match(r"[A-Za-z_][A-Za-z0-9_]*", stripped)
        if first_word is None:
            continue
        word = first_word.group(0)
        if word not in _TOP_LEVEL_TOKENS:
            continue
        if _indentation_of(lines[index]) <= parent_indent:
            return index, 0
    # Sin cierre natural: al final del archivo, en una nueva línea.
    last_line = source.count("\n")
    last_col = len(source) - (source.rfind("\n") + 1)
    return last_line, last_col

def _fix_close_pending(source: str, diagnostic) -> Iterator[QuickFix]:
    """Sugiere cerrar los pares ``()`` ``[]`` ``{}`` pendientes.

    Para cada par abierto, calcula el punto de cierre con
    ``_find_closure_position``: normalmente antes del siguiente bloque de
    nivel superior, o al final del archivo si no hay uno. Los cierres se
    agrupan en una sola edición para que el usuario pueda deshacer el fix
    de una vez.
    """
    abiertos, _huerfanos = _scan_pairs(source)
    if not abiertos:
        return
    cierres_por_posicion: dict[tuple[int, int], list[str]] = {}
    for pair in abiertos:
        line, column = _find_closure_position(source, pair)
        cierres_por_posicion.setdefault((line, column), []).append(
            _OPENERS[pair.opening]
        )
    if not cierres_por_posicion:
        return
    edits = []
    for (line, column), cierres in sorted(cierres_por_posicion.items()):
        text = "".join(reversed(cierres)) + "\n"
        edits.append((line, column, text))
    etiquetas = ", ".join(
        f"'{_OPENERS[p.opening]}'" for p in reversed(abiertos)
    )
    if len(edits) == 1:
        line, column, text = edits[0]
        yield QuickFix(
            title=f"Cerrar {etiquetas}",
            line=line,
            column=column,
            replacement=text,
        )
        return
    for line, column, text in edits:
        yield QuickFix(
            title=f"Cerrar {etiquetas} (insertar en {line + 1}:{column + 1})",
            line=line,
            column=column,
            replacement=text,
        )

# ---------------------------------------------------------------------------
# Fix: falta '{' al abrir un bloque
# ---------------------------------------------------------------------------

def _fix_missing_open_brace(source: str, diagnostic) -> Iterator[QuickFix]:
    """Sugiere insertar ``{ … }`` antes del token que interrumpió la apertura.

    Se inserta el par completo ``{ }`` con el cursor entre ambos (marcador
    ``$0``). Así el usuario queda listo para escribir el cuerpo del bloque
    sin tener que cerrar a mano. El espacio de separación depende del carácter
    previo: si ya hay un espacio, no se agrega otro.
    """
    line = _diagnostic_line(diagnostic)
    column = _diagnostic_column(diagnostic)
    lines = source.split("\n")
    if line >= len(lines):
        return
    prefix = lines[line][:column]
    sep = "" if not prefix or prefix[-1].isspace() else " "
    yield QuickFix(
        title="Añadir '{ … }' antes del bloque",
        line=line,
        column=column,
        replacement=f"{sep}{{$0}}",
        is_snippet=True,
    )

# ---------------------------------------------------------------------------
# Fix: falta '[' al abrir una lista, llamada o índice
# ---------------------------------------------------------------------------

def _fix_missing_open_bracket(source: str, diagnostic) -> Iterator[QuickFix]:
    """Sugiere insertar ``[ … ]`` antes del token que interrumpió la apertura."""
    line = _diagnostic_line(diagnostic)
    column = _diagnostic_column(diagnostic)
    yield QuickFix(
        title="Añadir '[ … ]' antes de la lista o llamada",
        line=line,
        column=column,
        replacement="[$0]",
        is_snippet=True,
    )

# ---------------------------------------------------------------------------
# Fix: 'main' faltante
# ---------------------------------------------------------------------------

_MAIN_SNIPPET = (
    "routine B32 main[] {\n"
    "    output 0.\n"
    "}\n"
)

def _fix_missing_main(source: str, diagnostic) -> Iterator[QuickFix]:
    if re.search(r"\broutine\b[^{]*\bmain\b", source):
        return
    last_line = source.count("\n")
    last_col = len(source) - (source.rfind("\n") + 1)
    yield QuickFix(
        title="Añadir 'routine B32 main[] { output 0. }' al final",
        line=last_line,
        column=last_col,
        replacement=_MAIN_SNIPPET,
    )

# ---------------------------------------------------------------------------
# Fix: función sin 'output' garantizado
# ---------------------------------------------------------------------------

_ROUTINE_RE = re.compile(
    r"^\s*routine\s+(?P<tipo>[A-Za-z_][A-Za-z0-9_]*)\s+"
    r"(?P<nombre>[A-Za-z_][A-Za-z0-9_]*)\b",
    re.MULTILINE,
)

def _enclosing_routine(source: str, line: int):
    """Devuelve el match de la rutina que contiene la línea dada, o None."""
    ultimo = None
    for match in _ROUTINE_RE.finditer(source):
        lineas_previas = source.count("\n", 0, match.start())
        if lineas_previas <= line:
            ultimo = match
        else:
            break
    return ultimo

def _routine_closing_brace(source: str, routine_start: int) -> Optional[int]:
    """Offset de la '}' que cierra el cuerpo de la rutina, o None.

    Ignora ``~~ comentarios``, ``*** bloques ***`` y ``"cadenas"``.
    """
    start_body = source.find("{", routine_start)
    if start_body < 0:
        return None
    abiertos = 0
    i = start_body
    n = len(source)
    while i < n:
        if source.startswith("~~", i):
            nl = source.find("\n", i)
            if nl < 0:
                return None
            i = nl + 1
            continue
        if source.startswith("***", i):
            end_block = source.find("***", i + 3)
            if end_block < 0:
                return None
            i = end_block + 3
            continue
        if source[i] == '"':
            i += 1
            while i < n:
                if source[i] == "\\" and i + 1 < n:
                    i += 2
                    continue
                if source[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if source[i] == "{":
            abiertos += 1
        elif source[i] == "}":
            abiertos -= 1
            if abiertos == 0:
                return i
        i += 1
    return None

def _return_constant_for_type(piper_type: str) -> str:
    """Constante literal válida para el tipo declarado de una rutina."""
    if piper_type == "bool":
        return "false"
    if piper_type == "none":
        return "none"
    # B8, uB8, B16, uB16, B32, uB32 aceptan el literal 0.
    return "0"

def _fix_missing_output(source: str, diagnostic) -> Iterator[QuickFix]:
    """Sugiere añadir ``output <constante>`` antes de la '}' de la rutina.

    La constante depende del tipo de retorno declarado en la firma:
    ``false`` para ``bool``, ``none`` para ``none`` y ``0`` para los enteros
    de 8/16/32 bits. Se inserta justo antes de la '}' que cierra la rutina.
    """
    line = _diagnostic_line(diagnostic)
    rutina = _enclosing_routine(source, line)
    if rutina is None:
        return
    cierre = _routine_closing_brace(source, rutina.start())
    if cierre is None:
        return
    tipo = rutina.group("tipo") or "none"
    constante = _return_constant_for_type(tipo)
    line_cierre, col_cierre = _offset_to_line_col(source, cierre)
    yield QuickFix(
        title=f"Añadir 'output {constante}.' antes de cerrar "
              f"'{rutina.group('nombre')}'",
        line=line_cierre,
        column=col_cierre,
        replacement=f"    output {constante}.\n",
    )

# ---------------------------------------------------------------------------
# Fix: variable local sin inicializar
# ---------------------------------------------------------------------------

_UNINITIALIZED_RE = re.compile(
    r"La variable local '(?P<nombre>[A-Za-z_][A-Za-z0-9_]*)' se lee antes de recibir un valor"
)

def _fix_uninitialized_local(source: str, diagnostic) -> Iterator[QuickFix]:
    m = _UNINITIALIZED_RE.search(_diagnostic_message(diagnostic))
    if not m:
        return
    nombre = m.group("nombre")
    line = _diagnostic_line(diagnostic)
    # Si ya hay un 'store' a esa variable en el archivo, no es que falte
    # inicialización: es que el 'store' existente tiene otro error (por
    # ejemplo, un identificador no declarado en el lado derecho). Sugerir
    # otro 'store' taparía el error real. No emitimos fix.
    if re.search(rf"\b{re.escape(nombre)}\s+store\b", source):
        return
    yield QuickFix(
        title=f"[⚠] Inicializar '{nombre}' con 0 antes de leerlo",
        line=line,
        column=0,
        replacement=f"{nombre} store 0.\n",
        unsafe=True,
    )

# ---------------------------------------------------------------------------
# Fix: identificador no declarado
# ---------------------------------------------------------------------------

_UNDECLARED_RE = re.compile(
    r"El identificador '(?P<nombre>[A-Za-z_][A-Za-z0-9_]*)' no ha sido declarado"
)

def _iter_declared_names(source: str) -> Iterator[str]:
    """Nombres declarados con 'create' o como parámetros de 'routine'."""
    for match in re.finditer(
        r"\bcreate\s+\w+(?:\|[^|\n]*\|)?\s+([A-Za-z_][A-Za-z0-9_]*)", source
    ):
        yield match.group(1)
    for match in re.finditer(
        r"\broutine\s+\w+\s+\w+\s*\[([^\]]*)\]", source
    ):
        params = match.group(1)
        for param in params.split(","):
            tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", param)
            if tokens:
                yield tokens[-1]

def _edit_distance(a: str, b: str) -> int:
    """Distancia de Levenshtein clásica, O(len(a)*len(b))."""
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, start=1):
        cur = [i] + [0] * len(b)
        for j, cb in enumerate(b, start=1):
            cost = 0 if ca == cb else 1
            cur[j] = min(cur[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost)
        prev = cur
    return prev[-1]

_TYPE_NAMES = r"(?:B8|uB8|B16|uB16|B32|uB32|bool)"

def _collect_declared_types(source: str) -> dict[str, str]:
    """Mapea nombre -> tipo Piper declarado (variables y parámetros)."""
    tipos: dict[str, str] = {}
    for match in re.finditer(
        r"\bcreate\s+(?P<tipo>" + _TYPE_NAMES + r")"
        r"(?:\|[^|\n]*\|)?\s+(?P<nombre>[A-Za-z_][A-Za-z0-9_]*)",
        source,
    ):
        tipos.setdefault(match.group("nombre"), match.group("tipo"))
    for match in re.finditer(
        r"\broutine\s+(?P<ret>" + _TYPE_NAMES + r")\s+\w+\s*\[([^\]]*)\]",
        source,
    ):
        params = match.group(2)
        for param in params.split(","):
            m = re.match(r"\s*(?P<tipo>" + _TYPE_NAMES + r")\s+"
                         r"(?:\|[^|\n]*\|)?\s*(?P<nombre>[A-Za-z_][A-Za-z0-9_]*)",
                         param)
            if m:
                tipos.setdefault(m.group("nombre"), m.group("tipo"))
    return tipos

def _store_target_type(source: str, nombre: str, line: int) -> Optional[str]:
    """Tipo del destino del store en cuya expresión aparece ``nombre``.

    Busca, en las líneas cercanas a ``line`` (la línea del error), un
    patrón ``<destino> store <expr>`` tal que ``<expr>`` contenga
    ``nombre``. Devuelve el tipo Piper del destino si está declarado.
    """
    declared = _collect_declared_types(source)
    lines = source.split("\n")
    # Buscar primero en la línea del error y luego en las cercanas, porque
    # el compilador reporta el error en la columna del identificador, no en
    # la línea completa.
    for offset in (0, -1, 1, -2, 2):
        idx = line + offset
        if not 0 <= idx < len(lines):
            continue
        m = re.search(
            r"\b(?P<destino>[A-Za-z_][A-Za-z0-9_]*)\s+store\s+(?P<expr>[^\n]*)",
            lines[idx],
        )
        if not m:
            continue
        if not re.search(rf"\b{re.escape(nombre)}\b", m.group("expr")):
            continue
        tipo = declared.get(m.group("destino"))
        if tipo:
            return tipo
    return None

def _infer_declared_type(source: str, nombre: str, line: int) -> str:
    """Tipo Piper más probable para ``nombre`` a partir de su uso.

    Prioridad:

    1. Destino de un ``<destino> store … b …`` con ``<destino>`` declarado
       → ese tipo (la asignación fuerza el tipo).
    2. ``b store true``/``b store false`` → ``bool``.
    3. Uso en un contexto lógico (``and``, ``or``, ``xor``, ``~``) → ``bool``.
    4. Uso junto a una variable declarada de tipo Piper conocido → ese tipo.
    5. Uso junto a un literal entero → tipo más estrecho que quepa (B8/uB8…
       con preferencia por B32), o B32.
    6. Si nada aplica → ``B32`` (el tipo entero por defecto).
    """
    escaped = re.escape(nombre)

    # 1. El store manda.
    tipo_store = _store_target_type(source, nombre, line)
    if tipo_store:
        return tipo_store

    # 2. Destino de store con literal booleano.
    if re.search(rf"\b{escaped}\s+store\s+(true|false)\b", source):
        return "bool"

    # 3. Contexto lógico.
    if re.search(rf"\b{escaped}\b\s*(and|or|xor|~)", source) or \
       re.search(rf"(and|or|xor|~)\s*\b{escaped}\b", source):
        return "bool"

    # 4. Junto a una variable declarada de tipo conocido.
    declared = _collect_declared_types(source)
    for otro, tipo in declared.items():
        if otro == nombre:
            continue
        if re.search(rf"\b{escaped}\b[^\n]*\b{re.escape(otro)}\b", source) or \
           re.search(rf"\b{re.escape(otro)}\b[^\n]*\b{escaped}\b", source):
            return tipo

    # 5. Junto a un literal entero: elegir el tipo más estrecho que quepa.
    valores = re.findall(rf"\b{escaped}\b[^\n]*?\b(\d+)\b", source) + \
              re.findall(rf"\b(\d+)\b[^\n]*?\b{escaped}\b", source)
    if valores:
        max_val = max(int(v) for v in valores)
        if max_val <= 127:
            return "B8"
        if max_val <= 255:
            return "uB8"
        if max_val <= 32767:
            return "B16"
        if max_val <= 65535:
            return "uB16"
        if max_val <= 2147483647:
            return "B32"
        return "uB32"

    # 6. Fallback.
    return "B32"

def _fix_undeclared_identifier(source: str, diagnostic) -> Iterator[QuickFix]:
    """Sugiere declarar o renombrar un identificador no declarado.

    Dos estrategias:

    1. Si hay un nombre declarado con distancia de Levenshtein ≤ 2,
       sugerir "¿quisiste decir 'X'?".
    2. Si no, declarar la variable **con el tipo inferido del contexto**:
       - El destino del ``store`` que la usa manda.
       - Si no hay store, mirar el contexto (bool / entero / comparación).
    """
    m = _UNDECLARED_RE.search(_diagnostic_message(diagnostic))
    if not m:
        return
    nombre = m.group("nombre")
    line = _diagnostic_line(diagnostic)
    column = _diagnostic_column(diagnostic)
    end_line, end_column = _diagnostic_end(diagnostic)
    declared = list(dict.fromkeys(_iter_declared_names(source)))

    # 1. Renombrar por cercanía.
    if len(nombre) >= 3:
        cercanos = sorted(
            (declared_name for declared_name in declared
             if declared_name != nombre
             and _edit_distance(nombre, declared_name) <= 2),
            key=lambda declared_name: _edit_distance(nombre, declared_name),
        )
        if cercanos:
            sugerido = cercanos[0]
            yield QuickFix(
                title=f"Reemplazar '{nombre}' por '{sugerido}'",
                line=line,
                column=column,
                replacement=sugerido,
                end_line=end_line if end_line is not None else line,
                end_column=end_column if end_column is not None else column + len(nombre),
            )
            return

    # 2. Declarar con el tipo inferido.
    tipo = _infer_declared_type(source, nombre, line)
    # Anclar la declaración al inicio del store que la usa, si lo hay.
    store_line = _store_line_containing(source, nombre, line)
    anchor_line = store_line if store_line is not None else line
    yield QuickFix(
        title=f"Declarar '{nombre}' como {tipo} antes del store",
        line=anchor_line,
        column=0,
        replacement=f"create {tipo} {nombre}.\n",
        unsafe=True,
    )

def _store_line_containing(source: str, nombre: str, line: int) -> Optional[int]:
    """Línea del store que usa ``nombre``, o None si no se encuentra."""
    lines = source.split("\n")
    for offset in (0, -1, 1, -2, 2):
        idx = line + offset
        if not 0 <= idx < len(lines):
            continue
        m = re.search(
            r"\b[A-Za-z_][A-Za-z0-9_]*\s+store\s+([^\n]*)",
            lines[idx],
        )
        if m and re.search(rf"\b{re.escape(nombre)}\b", m.group(1)):
            return idx
    return None

# ---------------------------------------------------------------------------
# Fix: comentario de bloque sin cerrar
# ---------------------------------------------------------------------------

def _fix_unclosed_block_comment(source: str, diagnostic) -> Iterator[QuickFix]:
    if source.count("***") % 2 == 0:
        return
    last_line = source.count("\n")
    last_col = len(source) - (source.rfind("\n") + 1)
    sep = "" if source.endswith((" ", "\n")) else " "
    yield QuickFix(
        title="Cerrar comentario de bloque con ' ***' al final del archivo",
        line=last_line,
        column=last_col,
        replacement=f"{sep}***\n",
    )

# ---------------------------------------------------------------------------
# Fallback por mensaje (compiladores que aún no emiten 'code')
# ---------------------------------------------------------------------------

def _fallback_from_message(source: str, diagnostic) -> Iterator[QuickFix]:
    message = _diagnostic_message(diagnostic)
    phase = _diagnostic_phase(diagnostic)
    if not message:
        return

    if phase == "lexical" and "comentario de bloque" in message:
        yield from _fix_unclosed_block_comment(source, diagnostic)
        return
    if phase == "syntax":
        if "Falta '.'" in message:
            yield from _fix_missing_dot(source, diagnostic)
            return
        if ("Se esperaba '}'" in message
                or "Se esperaba ']'" in message
                or "bloque sin cerrar" in message
                or "fin del archivo" in message):
            yield from _fix_close_pending(source, diagnostic)
            return
        # Mensajes concretos del parser cuando esperaba un delimitador de
        # apertura. El nuevo `code` los reemplaza, pero el fallback se
        # mantiene para compiladores antiguos.
        if "Se esperaba {" in message:
            yield from _fix_missing_open_brace(source, diagnostic)
            return
        if "Se esperaba [" in message:
            yield from _fix_missing_open_bracket(source, diagnostic)
            return
    if phase == "semantic":
        if "main" in message and "no puede recibir parámetros" not in message:
            yield from _fix_missing_main(source, diagnostic)
            return
        if "sin ejecutar output" in message:
            yield from _fix_missing_output(source, diagnostic)
            return
        if "se lee antes de recibir un valor" in message:
            yield from _fix_uninitialized_local(source, diagnostic)
            return
        if "no ha sido declarado" in message:
            yield from _fix_undeclared_identifier(source, diagnostic)
            return

# ---------------------------------------------------------------------------
# Punto de entrada
# ---------------------------------------------------------------------------

_CODE_DISPATCH = {
    "missing-dot": _fix_missing_dot,
    "missing-close-brace": _fix_close_pending,
    "missing-close-bracket": _fix_close_pending,
    "missing-close-paren": _fix_close_pending,
    "missing-open-brace": _fix_missing_open_brace,
    "missing-open-bracket": _fix_missing_open_bracket,
    "unclosed-block-comment": _fix_unclosed_block_comment,
    "missing-main": _fix_missing_main,
    "missing-output": _fix_missing_output,
    "uninitialized-local": _fix_uninitialized_local,
    "undeclared-identifier": _fix_undeclared_identifier,
}

def get_quick_fixes(source_code: str, diagnostic) -> list[QuickFix]:
    """Devuelve las correcciones sugeridas para ``diagnostic``.

    Prioriza ``diagnostic.code`` y, si no hay coincidencia, cae a la heurística
    por mensaje. Los diagnósticos sin corrección conocida devuelven una lista
    vacía (el editor no muestra bombillo, sólo el subrayado).
    """
    if source_code is None or diagnostic is None:
        return []
    code = _diagnostic_code(diagnostic)
    handler = _CODE_DISPATCH.get(code)
    if handler is not None:
        fixes = list(handler(source_code, diagnostic))
        if fixes:
            return fixes
    return list(_fallback_from_message(source_code, diagnostic))
