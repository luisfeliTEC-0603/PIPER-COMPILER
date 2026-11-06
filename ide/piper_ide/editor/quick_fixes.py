from dataclasses import dataclass
from typing import List, Optional
import re

@dataclass(frozen=True)
class QuickFix:
    """Describe una corrección de texto propuesta.

    Atributos:
        title: El texto que se mostrará al usuario.
        line: La línea (base 0) donde se aplica la corrección.
        column: La columna (base 0, en unidades UTF-16) donde se aplica.
        replacement: El texto que reemplazará al rango especificado.
        is_snippet: Si es `True`, `replacement` es un snippet (ej. "$0") y el
                    editor debe posicionar el cursor en el marcador.
    """
    title: str
    line: int
    column: int
    replacement: str
    is_snippet: bool = False
    end_line: Optional[int] = None
    end_column: Optional[int] = None

_SENTENCE_START = re.compile(r"^\s*(create|store|output|break|bring|return)\b")
_CONTROL_KEYWORDS = re.compile(r"^\s*(when|alwhen|loop|routine)\b")
_IGNORE_PATTERNS = re.compile(r"^\s*(~|\*\*\*|\})")

def _needs_terminator(line_text: str) -> bool:
    """Estima si una línea de código debería terminar con un punto."""
    stripped = line_text.strip()
    if not stripped or _IGNORE_PATTERNS.match(stripped):
        return False
    if stripped.endswith((".", "{", "}", "]", ")", "***")):
        return False
    if _CONTROL_KEYWORDS.match(stripped) and not stripped.endswith("{"):
        # `routine` y `loop` pueden no terminar en `{` si se declaran en varias líneas,
        # pero `when` y `alwhen` casi siempre lo hacen.
        pass
    
    # Si la línea contiene una asignación o es una llamada a función, es muy probable que necesite un punto.
    if " store " in line_text or "(" in line_text or _SENTENCE_START.match(stripped):
        return True
    return False

def _find_unclosed_brace(source_text: str) -> Optional[int]:
    """Encuentra la posición de la última llave '{' sin cerrar."""
    balance = 0
    last_open = -1
    in_single_comment = False
    in_block_comment = False
    for i, char in enumerate(source_text):
        # Simplificación de comentarios: no maneja comentarios anidados o en strings.
        # El lexer real es más robusto, pero para esta heurística es suficiente.
        if source_text.startswith("~~", i) and not in_block_comment:
            in_single_comment = True
        if char == '\n':
            in_single_comment = False
        if source_text.startswith("***", i) and not in_single_comment:
            in_block_comment = not in_block_comment
            # Avanzar para no procesar los asteriscos de nuevo
            continue 
        
        if in_single_comment or in_block_comment:
            continue

        if char == '{':
            balance += 1
            last_open = i
        elif char == '}':
            balance -= 1
    return last_open if balance > 0 else None

def _find_unclosed_paren(line_text: str) -> Optional[int]:
    """Encuentra la posición del último '(' sin cerrar en una línea."""
    balance = 0
    last_open = -1
    in_string = False
    for i, char in enumerate(line_text):
        if char == '"':
            in_string = not in_string
        if in_string:
            continue
        if char == '(':
            balance += 1
            last_open = i
        elif char == ')':
            balance -= 1
    return last_open if balance > 0 else None

# --- Generador de Correcciones ---

def get_quick_fixes(source_code: str, diagnostic_message: str, line: int, column: int) -> List[QuickFix]:
    """Analiza un diagnóstico y el código fuente para generar correcciones.
    
    Args:
        source_code: El texto completo del documento.
        diagnostic_message: El mensaje de error provisto por el compilador.
        line: La línea (base 0) del error.
        column: La columna (base 0) del error.

    Returns:
        Una lista de posibles `QuickFix`.
    """
    fixes: List[QuickFix] = []
    lines = source_code.splitlines()

    # --- Corrección: Falta '.' al final de la instrucción ---
    if "Falta '.'" in diagnostic_message:
        # El compilador reporta la posición de inserción, que es el final del token anterior.
        # En la práctica, usar la posición del error es un buen punto de partida.
        # Para ser más precisos, buscamos el final de la línea donde se reporta el error.
        try:
            target_line_text = lines[line]
            # Encontrar la última posición no-espacio
            stripped_len = len(target_line_text.rstrip())
            fixes.append(QuickFix(
                title="Añadir '.' al final de la instrucción",
                line=line,
                column=stripped_len,
                replacement="."
            ))
        except IndexError:
            pass # No hacer nada si la línea no existe.

    # --- Corrección: Falta '}' ---
    # Esta es una heurística a nivel de archivo.
    if "fin del archivo" in diagnostic_message or "block sin cerrar" in diagnostic_message:
        open_brace_pos = _find_unclosed_brace(source_code)
        if open_brace_pos is not None:
            # Calcular línea y columna de la llave abierta
            lines_before = source_code[:open_brace_pos].splitlines()
            brace_line = len(lines_before) - 1
            brace_col = len(lines_before[-1]) if lines_before else 0
            
            # Sugerir cerrar el bloque al final del archivo
            last_line = len(lines) - 1
            last_col = len(lines[-1]) if lines else 0
            fixes.append(QuickFix(
                title=f"Cerrar bloque con '}}' al final del archivo",
                line=last_line,
                column=last_col,
                replacement="\n}"
            ))

    # --- Corrección: Falta ')' ---
    # Heurística a nivel de línea, similar a la del punto.
    if "Se esperaba ')'" in diagnostic_message or "paréntesis" in diagnostic_message:
        try:
            target_line_text = lines[line]
            open_paren_pos = _find_unclosed_paren(target_line_text)
            if open_paren_pos is not None:
                # Sugerir cerrar el paréntesis al final del token actual.
                # Es una simplificación; una solución más compleja analizaría la expresión.
                stripped_len = len(target_line_text.rstrip())
                fixes.append(QuickFix(
                    title="Añadir ')' para cerrar el paréntesis",
                    line=line,
                    column=stripped_len,
                    replacement=")"
                ))
        except IndexError:
            pass

    return fixes
