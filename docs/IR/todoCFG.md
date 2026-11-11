# Contrato y tareas del constructor de CFG

Estado: C1 implementado; C2–C5 pendientes. El módulo IR ya genera bloques y terminadores y exporta `.ir`. El CFG conectará esos bloques; no vuelve a generarlos. Entradas y propiedad del IR: [contrato-IR.md](contrato-IR.md).

## 1. Flujo y alcance

```text
AST validado + tabla final
    -> ir_generate
    -> IrProgram: rutinas, bloques, instrucciones y terminadores
        -> ir_write: archivo .ir para inspección
        -> cfg_build: conexiones entre los bloques existentes
            -> cfg_write_dot: archivo .dot para inspección
            -> optimizador futuro: consultar conexiones y transformar IR
```

`.ir` y `.dot` son exportaciones. No se leen para comunicar módulos. El optimizador necesitará las instrucciones y sus efectos del mismo `IrProgram`, además de las conexiones del CFG; el dibujo del grafo por sí solo no contiene todo lo necesario para optimizar.

| Incluido | Excluido |
| --- | --- |
| Un grafo de flujo normal por rutina. | Parser de .ir, recorrido del AST o nueva división en bloques. |
| ENTRY, BLOCK, EXIT; aristas y vecinos. | Conexiones de llamadas entre funciones. |
| Alcanzabilidad desde ENTRY. | Liveness, dominadores, DCE, unrolling y asignación de registros. |
| Exportación DOT e integración opcional con pipoc. | Migración del codegen o cambios en el scheduler. |

El núcleo es menor que el generador IR: no traduce expresiones, matrices ni intrínsecas. Su trabajo principal es resolver IDs y conectar terminadores; también requiere administrar memoria, recorrer alcanzabilidad y exportar correctamente.

## 2. Entradas, salidas y propiedad

| Elemento | Regla concreta |
| --- | --- |
| Entrada | `const IrProgram *ir` de `ir_generate`, completo y validado, con tabla final aún viva. |
| Bloques | Usar `IrBlock.id`. No asumir que el ID coincide con la posición en `routine.blocks`. |
| Entrada de rutina | Usar `IrRoutine.entry`, aunque no sea el primer bloque físico. |
| Terminador | Última instrucción de cada bloque; leer `op` y `extra.targets` cuando corresponda. |
| Salida | `CfgProgram` con un grafo por rutina en el mismo orden que IR, incluido el arranque sintético si existe. |
| Propiedad | CFG posee nodos, aristas y listas de vecinos. Presta IR y conserva IDs de bloques; no duplica instrucciones, tipos, firmas ni símbolos. |
| Error | Estado y diagnóstico localizado; salida NULL, liberar lo construido y conservar el primer fallo. No imprimir desde el constructor. |
| Duración | Mantener IR y tabla final vivos mientras se consulte el CFG. Limpieza: CFG → IR → AST → tabla. |

Las matrices, intrínsecas y llamadas se mantienen como instrucciones del bloque. No requieren reglas de conexiones nuevas porque no son terminadores.

## 3. Modelo y API propuestos

| Estructura | Contenido mínimo |
| --- | --- |
| `CfgProgram` | Referencia prestada a IR y arreglo propio de grafos por rutina. |
| `CfgRoutine` | Índice de rutina IR, nodos, aristas e índices de ENTRY/EXIT. |
| `CfgNode` | Clase ENTRY/BLOCK/EXIT, ID del bloque si aplica, alcanzable y listas de sucesores/predecesores. |
| `CfgEdge` | Índice de nodo origen/destino y clase ENTRY/JUMP/TRUE/FALSE/RETURN. |

Los índices de nodos pertenecen al CFG; los IDs de bloques pertenecen al IR. Se necesitan ENTRY/EXIT sin bloque asociado, por lo que no se deben confundir ambos identificadores. Las aristas se guardan una vez; las listas de vecinos contienen índices de nodos sin duplicados.

```c
CfgStatus cfg_build(const IrProgram *ir, CfgProgram **out,
                    CfgDiagnostic *diagnostic);
CfgStatus cfg_validate(const CfgProgram *cfg, CfgDiagnostic *diagnostic);
CfgStatus cfg_write_dot(FILE *output, const CfgProgram *cfg);
void cfg_program_free(CfgProgram *cfg);
```

`out` válido y `*out == NULL` al entrar; no sobrescribir ni liberar un programa previo si el llamador incumple esa precondición. Estados: CFG_OK, CFG_INVALID_INPUT, CFG_INVALID_GRAPH, CFG_OUT_OF_MEMORY y CFG_IO_ERROR. Diagnóstico con estado, rutina, bloque y posición de arista/instrucción cuando aplique. `cfg_program_free(NULL)` es seguro; no libera IR.

## 4. Reglas de construcción

| Origen | Conexiones |
| --- | --- |
| ENTRY | Una al bloque indicado por `routine.entry`. |
| JUMP | Una al bloque `extra.targets[0]`. |
| BRANCH | TRUE a `extra.targets[0]` y FALSE a `extra.targets[1]`. |
| RETURN | Una al EXIT de esa rutina. |
| EXIT | Ninguna. |

1. Crear todos los nodos antes de resolver destinos. Basta una búsqueda de ID entre los bloques de la rutina para obtener su índice de nodo; no hace falta agregar un mapa persistente al IR ni una tabla hash inicialmente.
2. Crear conexiones solo desde el terminador. No inferir continuación por el orden físico ni elegir una rama si la condición es constante.
3. Una rama con ambos destinos iguales conserva dos aristas etiquetadas, pero un único vecino en sucesores/predecesores.
4. Construir las listas de vecinos desde las mismas aristas; no mantener dos algoritmos independientes de conexiones.
5. Recorrer desde ENTRY, con una pila/lista de pendientes y marcas de visitado para terminar también ante ciclos. Conservar bloques inalcanzables y EXIT inalcanzable si corresponde; no crear aristas para forzar su alcance.
6. CALL continúa dentro del bloque. IR_MAY_TRAP conserva su fallo implícito; no añadir una arista a EXIT, que representa retorno normal.
7. El arranque tiene grafo propio. Su llamada a main no conecta ambos grafos.

Ejemplo mínimo a partir de terminadores ya existentes:

```text
IR:                         Aristas del CFG:
B0: if t0 jump B1 else B2    ENTRY -> B0
B1: jump B3                 B0 -TRUE-> B1
B2: jump B3                 B0 -FALSE-> B2
B3: return t1               B1 -> B3
                            B2 -> B3
                            B3 -> EXIT
```

## 5. Secuencia de implementación

**Alta:** necesaria para usar el grafo en memoria. **Media:** inspección e integración con comandos.

| ID | Prioridad | Qué hacer | Por qué / salida verificable |
| --- | --- | --- | --- |
| C1 — completado | Alta | Definir cfg.h y reservar/liberar grafos y nodos. Inicializar aristas y vecinos vacíos; liberar también sus futuras reservas parciales. | Modelo descartable incluso tras fallo parcial; IR permanece intacta. C2 reserva las conexiones al conocer sus tamaños. |
| C2 | Alta | Construir un grafo por rutina y resolver ENTRY, JUMP, BRANCH y RETURN mediante IDs. Derivar vecinos de las aristas. | Conexiones correctas sin analizar expresiones ni copiar TAC. |
| C3 | Alta | Marcar alcanzabilidad con un recorrido y revisar las invariantes estructurales de la sección 6 antes de entregar el grafo. | Ciclos terminan el recorrido; se conservan nodos inalcanzables y se detectan conexiones inconsistentes. |
| C4 | Media | Implementar cfg_write_dot con nombres por rutina, ramas, TAC escapado y efectos. Reutilizar el formato de IR. | DOT determinista y fiel al mismo modelo, sin otro parser ni otra traducción. |
| C5 | Media | Integrar --emit-cfg, rutas, exportación segura y limpieza en main/Makefile. Preparar fuentes pequeñas y DOT esperados para revisión manual. | Funciona con salida principal o modo ir; sin afectar revisión, clones, ASM directo ni salidas actuales. |

### Archivos y funciones

| Archivo | Responsabilidad |
| --- | --- |
| `include/cfg.h` | Modelo compacto, estados y cuatro funciones públicas. |
| `src/cfg/cfg_internal.h` | Una entrada interna de C1 para reservar almacenamiento; no amplía las cuatro funciones públicas. |
| `src/cfg/cfg.c` | Constructor y liberación; auxiliares privados para rutina, arista, vecinos y recorrido cuando realmente se necesiten. |
| `src/cfg/cfg_validate.c` | Revisión estructural reducida. |
| `src/cfg/cfg_print.c` | DOT, nombres únicos y escape de etiquetas. |
| `src/ir/ir_print.c` | Extraer el formato textual compartido que necesite DOT; evitar duplicar operadores, nombres ambiguos, tipos o flags. Mantener las cuatro entradas públicas del módulo IR. |
| `src/main.c` / `Makefile` | Generar IR una sola vez si se solicita IR o CFG; construir CFG solo cuando se solicite; exportar y limpiar. No enlazar CFG en porygon-schedule. |

C1 ya incorpora cfg.h, cfg_internal.h y cfg.c. No añadir más interfaces auxiliares que las necesarias al implementar cada tarea. Para C4, compartir formateadores internos en memoria; no escribir un .ir temporal ni interpretar sus líneas para fabricar DOT. Esta reutilización es el ajuste principal pendiente fuera del constructor.

### C1 implementado: almacenamiento y uso

| Elemento | Regla concreta |
| --- | --- |
| `cfg_program_create` | Función interna: IR prestada + salida inicialmente NULL → almacenamiento propio solo al completar todas las reservas. No equivale a `cfg_build`: todavía no conecta ni revisa un grafo completo. |
| Rutinas | Un `CfgRoutine` por rutina IR, en el mismo orden. Su campo `routine` guarda ese índice. |
| Nodos | Reservar exactamente cantidad de bloques + 2. Índice 0 para ENTRY, después nodos BLOCK en orden físico de IR, último índice para EXIT. Los nodos BLOCK conservan el ID real del bloque. |
| Ejemplo de identidad | `nodes[1].block = 9` significa que el nodo CFG de índice 1 representa B9. El índice 1 no se convierte en ID B1. La arista desde ENTRY al `IrRoutine.entry` se resolverá en C2. |
| Aristas y vecinos | Punteros NULL y cantidades 0. C2 reservará lo necesario tras conocer conexiones; no agregar capacidades ni funciones grow. |
| Alcanzabilidad | Inicialmente false para todos los nodos; C3 hará el recorrido. |
| `cfg_program_free` | Liberar vecinos de cada nodo, nodos, aristas, rutinas y programa. Acepta NULL y construcción parcial; no libera la IR prestada. |
| `creation_error` | Auxiliar privado que descarta reservas parciales y copia diagnóstico, sin imprimir. Una salida ya ocupada se rechaza sin sobrescribirla ni liberar el programa anterior. |

No se integra CFG en main/Makefile hasta C5. `cfg_build`, `cfg_validate` y `cfg_write_dot` permanecen declarados. Comprobación temporal de C1: reservas, IDs no consecutivos, préstamos intactos, descarte con listas parcialmente reservadas, entradas inválidas y límites de tamaño que abortan tras una reserva parcial. Sin runner ni pruebas permanentes en el repositorio.

## 6. Revisión estructural e invalidación

IR ya fue validada. No repetir compatibilidad de tipos, promociones, firmas, resolución semántica ni análisis de disponibilidad de temporales.

| Revisar | Alcance |
| --- | --- |
| Correspondencia | Una pareja ENTRY/EXIT y un nodo BLOCK por bloque de cada rutina, sin IDs duplicados. |
| Aristas | Índices existentes y conexiones/clases concordantes con el terminador; ninguna conexión entre rutinas. Rechazar destinos inexistentes al resolverlos. |
| Vecinos | Listas sin duplicados y correspondencia con las mismas aristas en ambos sentidos. |
| Alcanzabilidad | Calcular una vez durante construcción. Los casos de prueba permiten revisar el resultado; no repetir todo el recorrido dentro del validador. |
| Préstamos | Referencias/IDs pertenecientes a la IR prestada que sigue vigente. No añadir un sistema de versiones al IR para esta entrega. |

Si el optimizador futuro cambia bloques, entrada o terminadores, debe descartar/reconstruir el CFG antes de volver a consultarlo. Si solo cambia instrucciones normales y conserva esas conexiones e IDs, el grafo puede mantenerse: los datos de instrucciones se consultan en IR. La coordinación corresponde al llamador; el CFG no detecta modificaciones automáticamente.

El optimizador recibirá acceso al mismo IR y a su CFG. El grafo facilita recorridos y vecinos; las transformaciones de instrucciones se hacen sobre IR. No implementar aún el optimizador ni prometer que reciba únicamente un archivo DOT.

## 7. Exportación e integración

| Solicitud | Comportamiento planificado |
| --- | --- |
| `bin/pipoc programa.rwt --emit-cfg salida.dot` | Generar/validar IR en memoria, construir CFG y exportar DOT; continuar con la salida principal. No crear .ir salvo solicitud explícita. |
| `bin/pipoc ir programa.rwt --emit-cfg salida.dot` | Conservar la salida .ir del comando y añadir DOT, sin codegen. |
| `bin/pipoc programa.rwt --emit-ir salida.ir --emit-cfg salida.dot` | Una ejecución del frontend y una generación de IR, compartida por ambos exportadores. |
| `all` | Añadir CFG solo con --emit-cfg; conservar el resto de salidas. |
| --check, clones, LL(1), -x, conversión bin→mem | No construir CFG; rechazar la combinación con --emit-cfg. |
| --emit-cfg-opt | Pendiente de optimización; no implementarlo como copia del grafo inicial. |

DOT contiene subgrafos por rutina, nodos ENTRY/EXIT y bloques con el TAC/comentarios de flags del mismo IR. La etiqueta de rutina incluye firma y cabeceras de tipos. Usar identificadores como `r0_b0` para evitar colisiones entre rutinas; marcar nodos inalcanzables. Escapar comillas, barras invertidas y saltos de línea. Graphviz no es dependencia para construir el compilador ni escribir DOT.

Ampliar las validaciones de opciones y colisiones existentes, incluyendo rutas equivalentes/enlaces. Reutilizar el mecanismo de temporal y renombrado para DOT y preservar archivos anteriores ante fallo. No agregar un segundo frontend ni un sistema distinto de publicación. Limpieza: CFG → IR → AST → tabla.

La opción --emit-cfg sigue pendiente (comando 34 de [comandos.md](../comandos.md)). La exportación prevista se ilustra en [ejemplo-flujo-IR.md](ejemplo-flujo-IR.md); no confundir el ejemplo DOT con una salida disponible actualmente.

## 8. Casos de revisión manual — pendientes

| Caso | Entrada pequeña | Qué comprobar |
| --- | --- | --- |
| CFG-T1 | `tests/ir/scalar.rwt` | ENTRY → B0 → EXIT. |
| CFG-T2 | `tests/ir/branches.rwt` | TRUE/FALSE y unión de ambas ramas, sin plegar condición constante. |
| CFG-T3 | `tests/ir/loop_break.rwt` | Vuelta a condición y salida correcta de break. |
| CFG-T4 | Fuente con dos rutinas y una llamada, más un caso interno preparado con bloque inalcanzable. | Grafos separados; ninguna arista de llamada; marcas de alcance correctas. Variante con destino inexistente debe fallar. |
| CFG-T5 | IR interna preparada: BRANCH con ambos destinos al mismo bloque. | Dos aristas etiquetadas, un vecino y DOT estable al exportar dos veces. |

Dejar fuentes y archivos esperados para ejecutar desde terminal y comparar aristas/DOT como texto. No añadir runner Python ni pruebas de imágenes. Los casos internos T4/T5 requieren entradas preparadas porque el generador actual evita parte del código inalcanzable y no suele emitir una rama con destinos iguales; una fuente Piper normal no garantiza esas formas de IR.

No implementar estas pruebas durante esta revisión documental. Los casos de matrices/Feistel pueden reutilizarse después para inspeccionar etiquetas; no agregan reglas nuevas de conectividad.
