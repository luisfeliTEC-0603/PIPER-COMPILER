# Implementación del constructor de CFG

Estado: pendiente. Etapa separada posterior a [todoIR.md](todoIR.md), dentro de `pipoc`. Recibe la IR definida en [contrato-IR.md](contrato-IR.md).

## 1. Alcance y contrato

| Elemento | Regla |
| --- | --- |
| Entrada | `const IrProgram *` completo y validado, con rutinas, bloques y terminadores explícitos. |
| Salida | `CfgProgram` con un grafo por rutina, nodos de entrada/salida normal, aristas, sucesores, predecesores y marcas de alcanzabilidad. |
| Propiedad | El CFG posee nodos, aristas y adyacencias. Presta referencias/IDs de bloques; no copia ni modifica instrucciones. Liberar CFG antes de IR. |
| Error | Estado y diagnóstico; salida nula y ninguna construcción parcial publicada. |
| Fuera del alcance | Modificar AST, dividir bloques, optimizar, calcular liveness/dominadores, renombrar o calendarizar. |

Construir desde estructuras en memoria, sin leer `.ir` ni volver al AST. Las llamadas no conectan los grafos de dos rutinas.

## 2. Modelo y API propuestos

| Estructura | Campos mínimos |
| --- | --- |
| `CfgProgram` | Referencia prestada a IR y grafos ordenados por rutina. |
| `CfgRoutine` | ID de rutina, nodos, aristas, entrada y salida normal. |
| `CfgNode` | ID, clase `ENTRY`/`BLOCK`/`EXIT`, ID de bloque cuando aplique, alcanzable, sucesores y predecesores. |
| `CfgEdge` | Origen, destino y clase `ENTRY`, `JUMP`, `TRUE`, `FALSE` o `RETURN`. |

```c
CfgStatus cfg_build(const IrProgram *ir, CfgProgram **out,
                    CfgDiagnostic *diagnostic);
CfgStatus cfg_validate(const CfgProgram *cfg, CfgDiagnostic *diagnostic);
CfgStatus cfg_write_dot(FILE *output, const CfgProgram *cfg);
void cfg_program_free(CfgProgram *cfg);
```

`out` válido y `*out == NULL` al entrar. Estados: `CFG_OK`, `CFG_INVALID_INPUT`, `CFG_INVALID_GRAPH`, `CFG_OUT_OF_MEMORY`, `CFG_IO_ERROR`. Diagnóstico: estado, rutina, bloque/terminador si aplica y mensaje. Liberación segura para puntero nulo. El llamador también comprueba errores al cerrar el stream.

## 3. Módulos y métodos

**Alta:** necesario para construir un grafo correcto. **Media:** siguiente entrega de inspección/integración.

| Módulo | Prioridad | Métodos y responsabilidad |
| --- | --- | --- |
| `include/cfg.h` | Alta | Modelo, estados y API pública. |
| `src/cfg/cfg.c` | Alta | `cfg_build`, `build_routine`, `add_edge`, `build_adjacency`, `mark_reachable`, `cfg_program_free`. |
| `src/cfg/cfg_validate.c` | Alta | `cfg_validate`: correspondencia con terminadores, destinos y adyacencias. |
| `src/cfg/cfg_print.c` | Media | `cfg_write_dot`: nodos, etiquetas y ramas deterministas. |
| `src/main.c` / `Makefile` | Media | Bandera, rutas, vida de estructuras, fuentes/objetos y enlace de `pipoc`. |

## 4. Reglas de construcción

| Origen | Aristas |
| --- | --- |
| `ENTRY` | Una al bloque de entrada de la rutina. |
| `JUMP destino` | Una al destino. |
| `BRANCH condición, verdadero, falso` | Una `TRUE` y una `FALSE`, incluso si ambas llegan al mismo bloque. |
| `RETURN` | Una al nodo `EXIT` de la rutina. |
| `EXIT` | Ninguna. |

1. Crear mapa de IDs de bloques a nodos, independiente de su posición en el arreglo.
2. Crear todos los nodos antes de resolver aristas. Rechazar destinos inexistentes o de otra rutina.
3. Derivar aristas exclusivamente de la última instrucción de cada bloque (su terminador). No inferir caída por el orden del listado ni simplificar condiciones constantes.
4. Conservar etiquetas `TRUE`/`FALSE` distintas aunque coincida el destino; las listas de vecinos no repiten el mismo nodo.
5. Calcular predecesores y sucesores desde las mismas aristas y comprobar simetría.
6. Recorrer desde `ENTRY` para marcar alcanzables. Conservar nodos inalcanzables y ciclos sin camino a salida; no añadir aristas artificiales para hacer alcanzable `EXIT`.
7. Una llamada normal continúa en su bloque. Los efectos `IR_MAY_TRAP` mantienen salida de error implícita, conforme al contrato de IR; este CFG representa el flujo normal y no crea arista a `EXIT` para un error.
8. La rutina sintética de arranque, si existe, tiene grafo propio. Su llamada a `main` no se convierte en arista entre rutinas.

## 5. Validación y reconstrucción

| Invariante | Comprobación |
| --- | --- |
| Correspondencia | Exactamente un nodo `BLOCK` por bloque IR y una pareja `ENTRY`/`EXIT` por rutina. |
| Terminadores | Destinos y clases de arista coinciden exactamente con la IR. |
| Pertenencia | Ninguna arista cruza rutinas. |
| Adyacencias | Predecesores/sucesores consistentes y sin vecinos duplicados. |
| Alcanzabilidad | Marcas coinciden con recorrido desde entrada; salida puede ser inalcanzable. |
| Datos prestados | Los IDs referenciados siguen presentes en la misma versión de IR. |

Si una transformación futura cambia bloques o terminadores, invalidar y reconstruir el CFG antes de consultarlo. No recalcular IR desde AST. No usar un CFG antiguo sobre una IR modificada.

## 6. Exportación e integración

| Solicitud | Comportamiento requerido |
| --- | --- |
| `bin/pipoc programa.rwt --emit-cfg salida.dot` | Generar/validar IR, construir CFG y exportarlo; continuar con la salida principal actual. |
| `bin/pipoc ir programa.rwt --emit-cfg salida.dot` | Extender el modo solo IR para exportar también el grafo, sin invocar codegen. |
| Exportaciones optimizadas | `--emit-cfg-opt` queda para la etapa de optimización; no implementarlo como copia del CFG inicial. |

Ejecutar `cfg_build` después de `ir_generate`. Agregar ruta CFG a las validaciones de combinaciones y colisiones del CLI. Publicar DOT mediante temporal y renombrado tras escritura/cierre exitosos; preservar archivos previos si falla. Orden de limpieza: CFG → IR → AST → tabla.

El DOT contiene un subgrafo por rutina, IDs globalmente inequívocos como `r0_b0`, `ENTRY`/`EXIT`, instrucciones escapadas y aristas etiquetadas. La etiqueta de rutina incluye su firma y las cabeceras de variables, temporales y constantes; cada bloque usa el mismo TAC/comentarios de flags que `.ir`, con la notación acordada en el contrato. El grafo no reconstruye ni pierde tipos: presta referencias a los bloques IR. Ejemplo de ambas salidas: [ejemplo-flujo-IR.md](ejemplo-flujo-IR.md). Marcar visualmente nodos inalcanzables. Escapar comillas, barras invertidas y saltos de línea; Graphviz no es dependencia para compilar ni para escribir DOT.

## 7. Pruebas pendientes

| Caso | Prioridad | Entrada | Comprobación |
| --- | --- | --- | --- |
| CFG-T1 | Alta | IR-T1 de la guía IR. | `ENTRY → B0 → EXIT`. |
| CFG-T2 | Alta | IR-T2 de la guía IR. | Dos aristas etiquetadas y convergencia de ambas ramas; sin simplificar condición constante. |
| CFG-T3 | Alta | IR-T3 de la guía IR. | Vuelta a condición y salida de `break` correctas. |
| CFG-T4 | Alta | IR unitaria con dos rutinas, una llamada y un bloque no alcanzable. | Grafos separados, sin arista de llamada, y alcanzabilidad correcta. Variante con destino inexistente debe fallar. |
| CFG-T5 | Media | Rama cuyos dos destinos son el mismo bloque. | Dos aristas etiquetadas, un único vecino en adyacencia y DOT estable en dos exportaciones. |

Comparar conjuntos de aristas/adyacencias e invariantes; verificar DOT como texto, sin pruebas de píxeles. Reutilizar las fuentes pequeñas de IR. No implementar pruebas en esta revisión documental.

## 8. Referencias

| Fuente | Uso |
| --- | --- |
| `Apuntes_PresentacionOptimizacion.pdf`, diapositivas 14–15 | Bloques, conexiones, entrada/salida y predecesores/sucesores. |
| `proyecto-ii-grupal-perf-cache.pdf`, §6 | El CFG prepara el análisis de liveness para DCE y las transformaciones posteriores; no las implementa. |
| [asm_program_parser.c](../../src/assembler/asm_program_parser.c), [machine_program.h](../../src/assembler/machine_program.h) | Referencia de bloques/aristas existentes sobre ASM; mantener separado el modelo IR. |
| [scheduler.c](../../src/scheduler/scheduler.c) | Consumidor de máquina existente; no modificar su algoritmo en esta etapa. |
