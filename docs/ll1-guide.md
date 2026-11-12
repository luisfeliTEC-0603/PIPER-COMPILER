# Guía para entender y defender la tabla LL(1) de Piper

## 1. Qué hace cada archivo

El recorrido de construcción es:

```text
src/analysis/ll1/piper.ll1          Gramática declarativa
              |
              v
tools/generate_ll1.py              Validación + nullable + FIRST + FOLLOW + tabla
              |
              +--> build/analysis/ll1/grammar_symbols.h   Enumeraciones C
              +--> build/analysis/ll1/grammar_data.inc    Producciones y matriz C
              +--> build/analysis/ll1/table.html          Reporte opcional
                                     |
                                     v
src/analysis/ll1/grammar.c          Consulta e impresión de los datos compilados
              |
              v
src/analysis/ll1/ll1.c              Analizador predictivo para consultar anticipaciones
```

Python se ejecuta durante `make` cuando corresponde regenerar los archivos.
El ejecutable C no necesita Python para consultar la tabla. `make ll1-table`
ejecuta el mismo análisis para producir el reporte de estudio.

Distinción importante: el flujo de compilación actual de `src/main.c` invoca
`yyparse`, generado por Bison desde `src/analysis/parser.y`, para construir el
AST. La gramática LL(1) es una especificación declarativa separada, usada por
`--ll1` y por `--ll1-expected` para las sugerencias sintácticas del IDE.
No se convierte automáticamente la gramática de Bison a LL(1), ni esta tabla
construye el AST. Los cambios de sintaxis deben mantenerse consistentes entre
ambas gramáticas.

## 2. Cómo ver la tabla como una matriz

Desde la raíz del compilador:

```sh
make ll1-table
```

Abrir `build/analysis/ll1/table.html` en un navegador. Funciona sin servidor,
dependencias web ni conexión a Internet. Tiene encabezados fijos,
desplazamiento horizontal/vertical y filtros por fila y columna.

También se puede elegir otra ruta:

```sh
python3 tools/generate_ll1.py src/analysis/ll1/piper.ll1 \
  --kind html --output build/tabla-ll1.html
```

La matriz sigue la notación habitual de clase:

- Filas: no terminales.
- Columnas: terminales de anticipación (lookahead).
- Celda ocupada: lado derecho de la producción elegida.
- `ε`: aplicar una producción vacía; no consumir un token por esa producción.
- Celda en blanco: no hay producción aplicable; es un error sintáctico.
- `$`: fin de entrada, llamado `EOF` internamente.

La gramática actual tiene 50 no terminales, 53 terminales, 112 producciones y
400 celdas definidas. Estas cifras pueden cambiar al editar la gramática.
La columna `token inválido` se conserva y no tiene producciones aplicables.

`bin/pipoc --ll1` conserva la lista textual de celdas definidas. El reporte
HTML muestra la misma tabla, junto con nullable, FIRST, FOLLOW y las reglas.

## 3. Lectura del generador: empezar por las estructuras

Abrir `tools/generate_ll1.py` y seguir este orden:

1. `Symbol`, `Production` y `Grammar`.
2. `parse_grammar` y `parse_productions`.
3. `sequence_first`.
4. `analyze_grammar` y las etapas que coordina: `calculate_nullable`,
   `calculate_first`, `calculate_follow` y `build_predictive_table`.
5. `render_symbols`, `render_data` y `render_html`.
6. `main`.

`Symbol(name, display)` separa el identificador interno del texto mostrado.
Por ejemplo, `RBRACKET` se muestra como `]` y `EOF` como `$`.

`Production(lhs, rhs)` representa una sola alternativa. La regla
`A: B C | %empty;` produce dos objetos:

```python
Production("A", ("B", "C"))
Production("A", ())
```

El lado derecho de epsilon es una tupla vacía. **Epsilon no es un token**.
`Grammar` reúne terminales, no terminales, símbolo inicial y producciones.
`GrammarAnalysis` conserva el resultado: nullable, FIRST, FOLLOW y tabla.

`analyze_grammar` coordina las etapas en su orden de dependencia:

```python
nullable = calculate_nullable(grammar)
first = calculate_first(grammar, nullable)
follow = calculate_follow(grammar, nullable, first)
table = build_predictive_table(grammar, nullable, first, follow)
```

La lectura valida declaraciones duplicadas, símbolos desconocidos, símbolo
inicial válido, presencia de EOF y no terminales sin reglas. No transforma
la gramática para quitar recursión izquierda o factorizarla: esa forma ya
debe estar escrita en `piper.ll1`.

## 4. Nullable: quién puede desaparecer

Un no terminal es nullable si puede derivar la cadena vacía, quizá pasando
por otros no terminales.

`calculate_nullable(grammar)` comienza con `nullable = set()`.
Para cada producción `A -> X1 ... Xn`, agrega A si todos los Xi son no
terminales que ya son nullable.

```python
all(symbol in nonterminals and symbol in nullable
    for symbol in production.rhs)
```

Para `A -> ε`, `rhs` está vacío y `all(...)` devuelve `True`. Esto expresa
que la secuencia vacía ya puede desaparecer. Un terminal nunca es nullable.

Se repite hasta que no se agrega ningún no terminal. Esto es un cálculo
de **punto fijo**: cada recorrido propaga información conocida a otras reglas.
No es una búsqueda recursiva de todas las cadenas posibles.

## 5. `sequence_first`: FIRST de una secuencia

FIRST responde: ¿qué terminales pueden aparecer al inicio de una cadena
derivada de esta secuencia?

`sequence_first(sequence, terminals, nullable, first)` devuelve dos cosas:

```text
(terminales que pueden comenzar la secuencia, puede desaparecer toda la secuencia)
```

Lee de izquierda a derecha:

1. Si encuentra un terminal, lo agrega y termina con `False`.
2. Si encuentra un no terminal, incorpora su FIRST.
3. Si ese no terminal no es nullable, termina con `False`.
4. Si es nullable, continúa: el siguiente símbolo también podría quedar primero.
5. Si alcanza el final, devuelve `True`: toda la secuencia puede desaparecer.

Ejemplo: en `A B`, si A es nullable, el comienzo puede venir de A o de B.
Si A no es nullable, no se incorpora FIRST(B).

**Convención del código:** `first[A]` guarda únicamente terminales. La
posibilidad de epsilon se guarda por separado en `nullable`. El reporte
HTML vuelve a unir ambos para mostrar `FIRST(A) = {..., ε}`.

## 6. FIRST: propagar comienzos hasta estabilizar

`calculate_first(grammar, nullable)` inicializa FIRST vacío para cada no
terminal. Para cada `A -> α`, llama a `sequence_first(α, ...)` y une el
resultado con `first[A]`.

```python
before = len(first[production.lhs])
first[production.lhs].update(additions)
if len(first[production.lhs]) != before:
    changed = True
```

`update` hace unión de conjuntos: no duplica terminales. Comparar tamaños
detecta si entró información nueva, porque nunca se eliminan elementos.

Un solo recorrido puede no alcanzar: una regla puede necesitar información
descubierta más tarde. El orden de las reglas afecta cuántos recorridos se
necesitan, pero no el resultado final de este procedimiento.

## 7. FOLLOW: quién puede aparecer inmediatamente después

FOLLOW(A) contiene terminales que pueden aparecer después de A en una forma
derivada desde el símbolo inicial. No contiene epsilon.

`calculate_follow(grammar, nullable, first)` empieza con `EOF` en FOLLOW
del símbolo inicial:

```python
follow[grammar.start].add("EOF")
```

Para cada aparición de B en una regla `A -> α B β`:

1. Agrega FIRST(β), sin epsilon, a FOLLOW(B).
2. Si β puede desaparecer, agrega FOLLOW(A) a FOLLOW(B).

Eso es lo que calcula este fragmento:

```python
suffix_first, suffix_nullable = sequence_first(
    production.rhs[position + 1:], terminals, nullable, first
)
additions = set(suffix_first)
if suffix_nullable:
    additions.update(follow[production.lhs])
```

El slice `position + 1:` representa **todo el sufijo después de B**, no solo
el símbolo inmediatamente siguiente. Si B es el último símbolo, el sufijo
es vacío; por eso se propaga FOLLOW(A). Se repite hasta estabilizar.

## 8. Ejercicio completo a mano

Usar esta gramática pequeña, con S como símbolo inicial:

```text
S -> A B
A -> a | ε
B -> b | ε
```

Los resultados son:

| No terminal | Nullable | FIRST, notación de clase | FOLLOW |
| --- | --- | --- | --- |
| S | Sí | {a, b, ε} | {$} |
| A | Sí | {a, ε} | {b, $} |
| B | Sí | {b, ε} | {$} |

Por qué FOLLOW(A) incluye `$`: B puede desaparecer, entonces lo que sigue
a S también puede seguir a A. FOLLOW(A) incluye `b` por FIRST(B).

La tabla queda:

| | a | b | $ |
| --- | --- | --- | --- |
| S | A B | A B | A B |
| A | a | ε | ε |
| B | | b | ε |

La celda S/$ **no contiene ε**: selecciona `S -> A B`, y luego A y B
desaparecen. Que el lado derecho sea nullable no cambia la producción elegida.

## 9. Construcción de la tabla y conflictos

Para cada producción `A -> α`, `build_predictive_table`:

1. Coloca esa producción en M[A, a] para cada a de FIRST(α), sin epsilon.
2. Si α es nullable, también la coloca en M[A, b] para cada b de FOLLOW(A).
3. Si otra producción ya ocupaba una celda, lanza `GrammarError`.

Un conflicto significa que con el mismo no terminal y un solo token de
anticipación hay dos alternativas posibles. El generador no elige la primera
ni sobrescribe silenciosamente la celda.

Ejemplo de conflicto FIRST/FIRST: `A -> a | a b`.
Ejemplo de conflicto FIRST/FOLLOW, con S inicial: `S -> A a; A -> a | ε`.
En el segundo, M[A,a] recibe tanto `A -> a` como `A -> ε`.

Los ciclos terminan porque los conjuntos crecen de forma monótona sobre un
universo finito de símbolos. La validación LL(1) comprueba decisiones
predictivas; no comprueba tipos ni semántica del lenguaje y tampoco prueba
por sí sola equivalencia con la gramática de Bison.

## 10. Un ejemplo real de Piper para defender

Buscar estas reglas en `piper.ll1`:

```text
PARAMETER_LIST -> PARAMETER PARAMETER_LIST_TAIL | ε
PARAMETER_LIST_TAIL -> COMMA PARAMETER PARAMETER_LIST_TAIL | ε
```

En la declaración de rutina, `PARAMETER_LIST` va seguido de `RBRACKET` (`]`).
Entonces:

- `FIRST(ListaParametros') = {coma, ε}` en notación habitual.
- `FOLLOW(ListaParametros') = {]}`.
- M[ListaParametros', `,`] selecciona `, Parametro ListaParametros'`.
- M[ListaParametros', `]`] selecciona `ε`.

El parser no consume `]` al aplicar epsilon. Quita `ListaParametros'` de la
pila, y más adelante compara `]` con el terminal correspondiente de la pila.

En el reporte, seleccionar `ListaParametros'` y luego cada una de esas
columnas permite mostrar ambas decisiones.

## 11. De Python a la consulta C

En Python, `table[(no_terminal, terminal)]` guarda un índice de producción
que empieza en cero. En C se genera:

```c
static const uint16_t predictive_table[LL1_N_COUNT][LL1_T_COUNT];
```

`render_data` guarda `índice + 1`. Así, cero significa celda vacía y uno
significa producción de índice cero. Los inicializadores omitidos quedan en
cero por las reglas de inicialización de objetos estáticos de C.

`ll1_table_lookup`, en `grammar.c`, devuelve NULL si la celda es cero y,
si está ocupada, recupera `productions[encoded_production - 1]`.

`ll1.c` consulta esa función usando el no terminal en la cima de su pila y
el token de anticipación. Cuando expande una producción, apila el lado derecho
en orden inverso para que el primer símbolo quede arriba. Una producción
vacía no apila nada. `--ll1-expected` usa este mecanismo sobre un prefijo para
determinar qué terminales permitirían continuar; no es una revisión semántica.

## 12. Guion breve para explicar el diseño

> La gramática LL(1) está escrita en un archivo declarativo. Durante la
> construcción, Python calcula primero qué no terminales son anulables,
> después FIRST y FOLLOW mediante iteraciones hasta punto fijo. Con esos
> conjuntos asigna una producción a cada celda y rechaza cualquier conflicto.
> El resultado se emite como datos C, por lo que el ejecutable consulta la
> tabla sin recalcularla. El reporte HTML presenta esa misma matriz y sus
> conjuntos para inspeccionarlos.

Preguntas para practicar:

1. ¿Por qué FIRST de `A B` puede incluir terminales de B?
2. ¿Por qué FOLLOW usa el sufijo completo y no solo el símbolo siguiente?
3. ¿Por qué hay varios recorridos y cómo sabemos que terminan?
4. ¿Qué diferencia hay entre epsilon y una celda vacía?
5. ¿Por qué una producción nullable puede ocupar una columna de FOLLOW?
6. ¿Qué indica que dos producciones intenten ocupar la misma celda?
7. ¿Por qué la matriz C usa `índice + 1`?
8. ¿Qué diferencia hay entre este analizador LL(1) y `yyparse`?

Las respuestas están, respectivamente, en las secciones 5, 7, 6/9, 2, 8/9,
9, 11 y 1. Para validar los ejemplos y el generador:

```sh
python3 -m unittest discover -s tests/tools -p 'test_generate_ll1.py' -v
```
