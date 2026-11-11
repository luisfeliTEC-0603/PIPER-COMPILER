# Ejemplo completo: fuente → análisis → IR → CFG

El fuente de este documento se comprobó con el frontend actual: `--check` aprobó y `ins -t` produjo AST y símbolos. Las salidas `.ir` y `.dot` son **resultados esperados del contrato**, no archivos producidos por los módulos actuales, que todavía no tienen implementación. No se aplican optimizaciones.

## 1. Entrada al compilador

Contenido de `ejemplo.rwt`:

```text
routine B32 main[] {
    create B32 a.
    create B32 b.
    a store 2.
    when [a < 5] {
        b store a + 3.
    } fallback {
        b store a - 1.
    }
    output b.
}
```

El resultado de una futura ejecución correcta es 5: se toma la rama `2 < 5`, que asigna `2 + 3` a `b`. La generación de IR conserva ambas ramas; no ejecuta el programa ni propaga `a = 2` para eliminar una de ellas.

| Comando | Estado / salida |
| --- | --- |
| `bin/pipoc --check ejemplo.rwt` | Disponible: comprueba léxico, sintaxis y semántica; termina con estado 0 y sin archivos. |
| `bin/pipoc ins ejemplo.rwt -t` | Disponible: imprime AST y genera `ejemplo.ast.json` y `ejemplo.symbols.txt`. |
| `bin/pipoc ir ejemplo.rwt -o ejemplo.ir --emit-cfg ejemplo.dot` | Pendiente: exportará IR y CFG sin llamar a codegen. |

## 2. Resumen antes de IR

| Etapa | Qué ocurre con este fuente | Resultado |
| --- | --- | --- |
| Lexer | Reconoce `routine`, B32, identificadores, números, `when`, `fallback`, operadores y delimitadores. | Sin errores léxicos. |
| Parser Bison | Forma declaración de rutina, declaraciones locales, asignación, condicional y retorno. Respeta la estructura `a + 3` y `a - 1`. | AST válido. |
| Semántico | Registra `main[]`, `a` y `b`; comprueba tipos, uso inicializado de `a`, condición y retorno B32. `b` recibe valor en ambas ramas antes de `output`. | Análisis aprobado y tabla final disponible. |
| Orquestador | Comprueba `yyparse == 0`, AST no nulo y retorno semántico igual a 1. | Puede llamar al futuro `ir_generate` con AST y tabla de esta ejecución. |

No se vuelve a ejecutar el frontend dentro de IR. La referencia directa al símbolo final por uso es el ajuste pendiente del plan; en este ejemplo se denominarán `S_a` y `S_b` solo para explicar esos enlaces, no para imprimirlos en TAC.

## 3. Forma del AST

Vista resumida del AST obtenido; se omiten ubicaciones y campos vacíos. Es un árbol de nodos, no el TAC ni el formato literal del JSON:

```text
Program
└── RoutineDeclaration: main, retorno B32, parámetros []
    └── Block
        ├── VariableDeclaration: a, B32
        ├── VariableDeclaration: b, B32
        ├── Assignment
        │   ├── target: IdentifierExpression a
        │   └── value: IntegerLiteral 2
        ├── WhenStatement
        │   ├── condition: BinaryExpression less
        │   │   ├── IdentifierExpression a
        │   │   └── IntegerLiteral 5
        │   ├── then_block: Block
        │   │   └── Assignment
        │   │       ├── target: IdentifierExpression b
        │   │       └── value: BinaryExpression add
        │   │           ├── IdentifierExpression a
        │   │           └── IntegerLiteral 3
        │   └── fallback_block: Block
        │       └── Assignment
        │           ├── target: IdentifierExpression b
        │           └── value: BinaryExpression subtract
        │               ├── IdentifierExpression a
        │               └── IntegerLiteral 1
        └── OutputStatement
            └── IdentifierExpression b
```

Anotaciones relevantes que el semántico mantiene en memoria:

| Nodo / dato | Información usada por IR |
| --- | --- |
| Usos de `a` y `b` | Tipo exacto B32; enlace a sus declaraciones finales cuando se implemente el ajuste del contrato. |
| `a < 5` | Resultado bool; comparación de enteros B32. |
| `a + 3`, `a - 1` | Resultado B32 y semántica aritmética comprobada. |
| Literales 1, 2, 3 y 5 | Valores constantes; IR concretará B32 a partir de los usos y anotaciones. |
| `main` | Retorno B32, sin parámetros, ámbito de función. |

El JSON actual muestra la estructura del árbol; no imprime necesariamente todas las anotaciones semánticas internas.

## 4. Tabla de símbolos final

Resumen de la salida actual de `--emit-symbols`:

| Ámbito | Nombre | Clase | Tipo | Bytes | Ubicación actual |
| --- | --- | --- | --- | --- | --- |
| 0, global | `main` | Función | Retorno B32 | 0 | Etiqueta `main`. |
| 1, rutina `main` | `a` | Variable | B32 | 4 | `r7 + 0x0000`. |
| 1, rutina `main` | `b` | Variable | B32 | 4 | `r7 + 0x0004`. |
| 2, rama verdadera | — | — | — | — | Sin declaraciones propias. |
| 3, rama falsa | — | — | — | — | Sin declaraciones propias. |

Los offsets son datos del compilador actual; IR no los usa como identidad ni reserva pila para temporales. La generación consulta las declaraciones y tipos. `S_a` y `S_b` son referencias prestadas a las variables del ámbito 1, aunque se usen dentro de los ámbitos 2 y 3. Los temporales nuevos no se insertan en esta tabla.

## 5. Recorrido interno del generador

| Paso | Función / estado | Operaciones o resultado |
| --- | --- | --- |
| 1 | `ir_generate(root, symbols, &program, &diagnostic)` | Crea modelo provisional y contexto con `error.status = IR_OK`. Ningún resultado se publica aún. |
| 2 | `ir_build_program(ctx)` | Registra la rutina `main` con su `Symbol` final. No crea arranque sintético porque no hay sentencias globales. |
| 3 | `ir_new_block(ctx)` y selección de B0 | Establece entrada B0; `ir_stmt` recorre el cuerpo. Las declaraciones solo aportan datos ya existentes en la tabla. |
| 4 | `ir_stmt(a store 2)` | `ir_target(a)` devuelve `S_a`; `ir_expr(2)` devuelve constante B32. `ir_emit` agrega `a = 2`. |
| 5 | `ir_stmt(when ...)` → `ir_expr(a < 5)` | Captura `a` en `t0:B32`; crea `t1:bool` con `t0 < 5`. |
| 6 | Crear B1, B2 y B3; emitir condición | Agrega `if t1 jump B1 else B2` a B0 y cierra ese camino. B1 será verdadero, B2 falso y B3 unión. |
| 7 | Seleccionar B1; `ir_stmt(b store a + 3)` | Destino `S_b`; `ir_expr` crea `t2 = a` y `t3 = t2 + 3`. Emite `b = t3` y `jump B3`. |
| 8 | Seleccionar B2; `ir_stmt(b store a - 1)` | Mismo destino `S_b`; crea `t4 = a` y `t5 = t4 - 1`. Emite `b = t5` y `jump B3`. |
| 9 | Seleccionar B3; `ir_stmt(output b)` | Captura `b` en `t6:B32`; emite `return t6` y deja `ctx->block = IR_NO_ID`. |
| 10 | `ir_validate(program, diagnostic)` | Comprueba tipos, definición de temporales, cuatro bloques y destinos existentes; cada bloque tiene transferencia final. |
| 11 | Fin de `ir_generate` | Libera recursos del contexto y entrega propiedad del programa al llamador con `IR_OK`. |
| 12 | `ir_write(output, program)` | Recorre ese modelo: escribe cabeceras de tipos, instrucciones y todos los flags activos. No vuelve al AST ni genera otro TAC. |

Dentro de `ir_expr`, los literales se convierten en operandos y los identificadores se capturan antes de emitir la operación padre. Por ejemplo, `a + 3` produce lectura de `a`, constante 3 y finalmente suma. `ir_emit` es el mismo para copia, suma, condición, salto y retorno.

### Ejemplo de una instrucción en memoria

La instrucción de suma de B1 tendrá estos datos:

| Campo | Valor esperado |
| --- | --- |
| `op` | `IR_OP_ADD`. |
| `arg1` | Temporal ID 2, tipo B32, `address = false`. |
| `arg2` | Constante 3, tipo B32, `address = false`. |
| `result` | Temporal ID 3, tipo B32, `address = false`. |
| `flags` | `IR_CHECKED` e `IR_MAY_TRAP`. |
| `location` | Expresión `a + 3`, línea 6, columnas 17–22 según el AST actual. |

Se verá como `t3 = t2 + 3 ; comprueba rango, puede fallar`; su tipo aparece en la cabecera. Las copias escalares locales, comparación y transferencias de este ejemplo no tienen flags activos. No se confunde el resultado bool `t1` con los temporales enteros.

Al terminar: una rutina, cuatro bloques, siete temporales, catorce instrucciones contando los saltos/retorno y ningún destino de `break`. La tabla semántica sigue viva; no se le agregaron los temporales.

## 6. Salida esperada: `ejemplo.ir`

```text
function main() -> B32
    variables: local a:B32, local b:B32
    temporales: t0:B32, t1:bool, t2:B32, t3:B32, t4:B32, t5:B32, t6:B32
    constantes: 2:B32, 5:B32, 3:B32, 1:B32
B0:
    a = 2
    t0 = a
    t1 = t0 < 5
    if t1 jump B1 else B2
B1:
    t2 = a
    t3 = t2 + 3 ; comprueba rango, puede fallar
    b = t3
    jump B3
B2:
    t4 = a
    t5 = t4 - 1 ; comprueba rango, puede fallar
    b = t5
    jump B3
B3:
    t6 = b
    return t6
```

Los temporales/constantes se listan en orden de primera aparición. La cabecera es parte de la exportación de IR, no código Piper. Sin comentario de flags equivale a flags=0. Las variables siguen siendo símbolos reasignables; no se introduce SSA ni una función φ para combinar las asignaciones de `b`.

## 7. Construcción posterior del CFG

| Paso | Entrada → salida |
| --- | --- |
| `cfg_build(program, &cfg, &diagnostic)` | Recibe el mismo `IrProgram`; crea nodos ENTRY, B0–B3 y EXIT para `main`. |
| Consultar últimas instrucciones | B0 aporta dos destinos; B1 y B2 saltan a B3; B3 retorna. No consulta el AST. |
| Calcular adyacencias | Registra las conexiones de la tabla siguiente y sus predecesores. |
| Marcar alcanzables y validar | Los seis nodos son alcanzables estructuralmente. No evalúa la condición constante para quitar B2. |
| `cfg_write_dot(output, cfg)` | Imprime firma/metadatos y las mismas instrucciones como etiquetas, con sus aristas. |

| Origen | Destino | Clase |
| --- | --- | --- |
| ENTRY | B0 | Entrada. |
| B0 | B1 | Verdadero. |
| B0 | B2 | Falso. |
| B1 | B3 | Salto. |
| B2 | B3 | Salto. |
| B3 | EXIT | Retorno. |

## 8. Salida esperada: `ejemplo.dot`

DOT es el archivo de descripción del CFG; un visor podrá dibujarlo. `r0` identifica la rutina 0 y evita colisiones si después se agregan más funciones. La firma y las listas de tipos forman la cabecera del subgrafo; no se convierten en nodos de ejecución.

```dot
digraph EjemploIR {
    subgraph cluster_r0 {
        label="main() -> B32\nvariables: local a:B32, local b:B32\ntemporales: t0:B32, t1:bool, t2:B32, t3:B32, t4:B32, t5:B32, t6:B32\nconstantes: 2:B32, 5:B32, 3:B32, 1:B32";
        r0_entry [label="ENTRY"];
        r0_b0 [shape=box, label="B0:\la = 2\lt0 = a\lt1 = t0 < 5\lif t1 jump B1 else B2\l"];
        r0_b1 [shape=box, label="B1:\lt2 = a\lt3 = t2 + 3 ; comprueba rango, puede fallar\lb = t3\ljump B3\l"];
        r0_b2 [shape=box, label="B2:\lt4 = a\lt5 = t4 - 1 ; comprueba rango, puede fallar\lb = t5\ljump B3\l"];
        r0_b3 [shape=box, label="B3:\lt6 = b\lreturn t6\l"];
        r0_exit [label="EXIT"];
        r0_entry -> r0_b0 [label="entrada"];
        r0_b0 -> r0_b1 [label="verdadero"];
        r0_b0 -> r0_b2 [label="falso"];
        r0_b1 -> r0_b3 [label="salto"];
        r0_b2 -> r0_b3 [label="salto"];
        r0_b3 -> r0_exit [label="retorno"];
    }
}
```

`\l` separa líneas alineadas a la izquierda dentro de una etiqueta DOT. No forma parte del TAC.

## 9. Entrega a los consumidores

| Consumidor | Qué recibe de este ejemplo |
| --- | --- |
| Optimizador futuro | Programa con cuatro bloques, operaciones tipadas, efectos y CFG de seis conexiones. Podrá analizar/reducirlo en una etapa posterior. |
| Codegen futuro | IR optimizada y declaraciones referenciadas; distinguirá B32/bool por los operandos y podrá consultar símbolos. El CFG no sustituye las instrucciones. |
| Lector humano | `.ir` con tipos/efectos y `.dot` con conexiones; ambos describen el mismo modelo. |

Liberación posterior: CFG → IR → AST → tabla de símbolos. El ejemplo documenta el flujo y no agrega implementación ni una prueba automatizada nueva.
