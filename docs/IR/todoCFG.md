# Contrato y tareas del constructor de CFG

Estado: C1–C5 implementados. Exportación disponible mediante --emit-cfg y ejemplos manuales en tests/cfg. El módulo IR ya genera bloques y terminadores y exporta `.ir`. El CFG conecta esos bloques; no vuelve a generarlos. Entradas y propiedad del IR: [contrato-IR.md](contrato-IR.md).

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
| C2 — completado | Alta | Conectar cada rutina con ENTRY, JUMP, BRANCH y RETURN mediante IDs y derivar vecinos de las mismas aristas. | Conexiones correctas sin analizar expresiones ni copiar TAC; C3 completa revisión y alcance. |
| C3 — completado | Alta | Revisar estructura, recorrer desde ENTRY y entregar el programa solo en éxito. | Ciclos terminan el recorrido; se conservan nodos y EXIT inalcanzables y se detectan conexiones inconsistentes. |
| C4 — completado | Media | Implementar cfg_write_dot con nombres por rutina, ramas, TAC escapado y efectos. Reutilizar el formato de IR. | DOT determinista y fiel al mismo modelo, sin otro parser ni otra traducción. |
| C5 — completado | Media | Integrar --emit-cfg, rutas, exportación segura y limpieza en main/Makefile. Preparar fuentes pequeñas y DOT esperados para revisión manual. | Funciona con salida principal o modo ir; sin afectar revisión, clones, ASM directo ni salidas actuales. |

### Archivos y funciones

| Archivo | Responsabilidad |
| --- | --- |
| `include/cfg.h` | Modelo compacto, estados y cuatro funciones públicas. |
| `src/cfg/cfg_internal.h` | Dos entradas internas: reservar almacenamiento y conectarlo. No amplía las cuatro funciones públicas. |
| `src/cfg/cfg.c` | Constructor y liberación; auxiliares privados para rutina, arista, vecinos y recorrido cuando realmente se necesiten. |
| `src/cfg/cfg_validate.c` | Revisión estructural reducida. |
| `src/cfg/cfg_print.c` | DOT, nombres únicos y escape de etiquetas. |
| `src/ir/ir_print.c` | Extraer el formato textual compartido que necesite DOT; evitar duplicar operadores, nombres ambiguos, tipos o flags. Mantener las cuatro entradas públicas del módulo IR. |
| `src/main.c` / `Makefile` | Generar IR una sola vez si se solicita IR o CFG; construir CFG solo cuando se solicite; exportar y limpiar. No enlazar CFG en porygon-schedule. |

Los formateadores de C4 se comparten mediante src/ir/ir_format.h. No se escribe un .ir temporal ni se interpretan sus líneas para fabricar DOT; las cuatro funciones públicas de IR se conservan.

### C1 implementado: almacenamiento y uso

| Elemento | Regla concreta |
| --- | --- |
| `cfg_program_create` | Función interna: IR prestada + salida inicialmente NULL → almacenamiento propio solo al completar todas las reservas. No equivale a `cfg_build`: todavía no conecta ni revisa un grafo completo. |
| Rutinas | Un `CfgRoutine` por rutina IR, en el mismo orden. Su campo `routine` guarda ese índice. |
| Nodos | Reservar exactamente cantidad de bloques + 2. Índice 0 para ENTRY, después nodos BLOCK en orden físico de IR, último índice para EXIT. Los nodos BLOCK conservan el ID real del bloque. |
| Ejemplo de identidad | `nodes[1].block = 9` significa que el nodo CFG de índice 1 representa B9. El índice 1 no se convierte en ID B1. La arista desde ENTRY al `IrRoutine.entry` se resolverá en C2. |
| Aristas y vecinos | Punteros NULL y cantidades 0. C2 reservará lo necesario tras conocer conexiones; no agregar capacidades ni funciones grow. |
| Alcanzabilidad | Inicialmente false para todos los nodos; cfg_build calcula después el alcance de cada rutina. |
| `cfg_program_free` | Liberar vecinos de cada nodo, nodos, aristas, rutinas y programa. Acepta NULL y construcción parcial; no libera la IR prestada. |
| `cfg_error` | Auxiliar privado que copia diagnóstico y puede descartar reservas propias. C1 pasa el programa parcial; C2 pasa NULL para conservar la propiedad en su llamador. No imprime. Una salida ya ocupada no se sobrescribe ni se libera. |

C5 ya integra CFG en main/Makefile. Las cuatro funciones públicas están implementadas. Comprobación temporal de C1: reservas, IDs no consecutivos, préstamos intactos, descarte con listas parcialmente reservadas, entradas inválidas y límites de tamaño que abortan tras una reserva parcial. Sin runner ni pruebas permanentes en el repositorio.

### C2 implementado: conexiones y asociaciones

| Función / paso | Regla concreta |
| --- | --- |
| `cfg_connect` | Entrada interna: almacenamiento nuevo de `cfg_program_create`. Completa aristas y vecinos; por sí sola no calcula alcance ni entrega desde la API pública. |
| Reserva de aristas | Contar una por ENTRY, una por JUMP/RETURN y dos por BRANCH; reservar una vez por rutina. Consultar únicamente el terminador, no tipos ni instrucciones intermedias. |
| `block_node` | Buscar el nodo BLOCK cuyo ID IR coincide con el destino, dentro de la misma rutina. Funciona con IDs dispersos, bloques reordenados y entrada distinta del primer bloque físico. |
| Condición | La expresión bool ya está en IR. BRANCH registra TRUE/FALSE hacia sus dos destinos, incluso para condición constante. El CFG no elige una salida ni vuelve a calcular la expresión. |
| `build_neighbors` | Contar grados desde las aristas, reservar espacio para vecinos y llenar ambos sentidos. Registrar solo vecinos distintos; una rama con destinos iguales puede dejar una casilla reservada sin uso, sin repetir vecino ni agregar capacidades al modelo. |
| RETURN / llamadas | RETURN conecta con EXIT. CALL/intrínsecas no agregan conexiones entre rutinas; el arranque sintético conserva su grafo separado. |
| Errores | Rechazar al resolver una entrada/destino inexistente o un bloque sin terminador utilizable. No repetir `ir_validate`. Conservar primer diagnóstico con rutina, bloque, terminador y arista disponibles. |
| Propiedad | También al fallar, el llamador interno conserva el CFG parcial y debe liberarlo. No reconectar una construcción ya utilizada; liberar y crear otra. `cfg_build` ya coordina esa limpieza. |

Uso interno de C1/C2: `cfg_program_create` → `cfg_connect`. Aún deja `reachable=false`. Para obtener un programa completo en memoria usar `cfg_build`, que añade C3 y entrega propiedad solo en éxito.

La información del grafo son conexiones, etiquetas de rama y listas derivadas de vecinos. ENTRY/EXIT son nodos sintéticos de análisis, no instrucciones nuevas. Los cálculos, condiciones, tipos y efectos se consultan en el IR prestado. Esto permitirá recorrer ciclos y propagar información entre predecesores/sucesores en análisis futuros; no realiza optimización por sí mismo.

Comprobación temporal de C2: cinco fuentes IR existentes, ramas con destino igual, vueltas de ciclo y break, IDs dispersos, bloques reordenados, entrada distinta del primer nodo BLOCK, funciones/arranque separados, vecinos sin duplicados y errores al resolver destinos. La comprobación de C2 fue temporal, sin runner; C5 ya integra main/Makefile.

### C3 implementado: revisión y alcanzabilidad

| Función / recurso | Regla concreta |
| --- | --- |
| `cfg_build` | Reservar con C1, conectar con C2, revisar estructura y recorrer desde ENTRY. Entregar el programa solo en éxito; ante fallo liberar todo lo propio y conservar diagnóstico. No tocar una salida previamente ocupada. |
| `cfg_validate` | Revisar correspondencia de rutinas/nodos, índices, etiquetas de aristas y listas de vecinos. Comparar el destino ya asociado con el ID almacenado en el terminador, sin volver a buscarlo ni llamar a ir_validate. No depende del orden de aristas. |
| Máscara de revisión | Un byte temporal por nodo registra salidas vistas: una para ENTRY/JUMP/RETURN; TRUE/FALSE para BRANCH. Detectar conexiones faltantes o repetidas, sin reconstruir el grafo. Se libera al terminar la rutina. |
| Vecinos | Revisar índices/listas sin duplicados y presencia de cada arista en ambos sentidos. Los totales de pares distintos permiten detectar vecinos sobrantes, incluida una rama con dos aristas y un solo vecino. |
| `mark_reachable` | Recorrido privado iterativo sobre sucesores, con pila del tamaño de nodos. Marcar al apilar para visitar cada nodo una sola vez y terminar ante ciclos. Liberar la pila después de cada rutina. |
| Orden de pasos | Revisar estructura antes de recorrer para poder usar índices seguros. El validador no repite el recorrido ni comprueba de nuevo las marcas. |

El alcance es estructural: seguir TRUE y FALSE sin evaluar la condición. Conservar nodos inalcanzables y EXIT inalcanzable en ciclos cerrados. No eliminar bloques, forzar una salida ni ejecutar instrucciones.

Comprobación temporal con sanitizadores: cinco fuentes IR, ramas con destino compartido, vecinos faltantes/duplicados/sobrantes, aristas reordenadas, bloque inalcanzable, ciclo cerrado sin camino a EXIT, funciones/arranque separados y descarte ante errores. La comprobación de C3 fue temporal, sin runner; C4/C5 ya exportan DOT desde CLI.

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

| Solicitud | Comportamiento implementado |
| --- | --- |
| `bin/pipoc programa.rwt --emit-cfg salida.dot` | Generar/validar IR en memoria, construir CFG y exportar DOT; continuar con la salida principal. No crear .ir salvo solicitud explícita. |
| `bin/pipoc ir programa.rwt --emit-cfg salida.dot` | Conservar la salida .ir del comando y añadir DOT, sin codegen. |
| `bin/pipoc programa.rwt --emit-ir salida.ir --emit-cfg salida.dot` | Una ejecución del frontend y una generación de IR, compartida por ambos exportadores. |
| `all` | Añadir CFG solo con --emit-cfg; conservar el resto de salidas. |
| --check, clones, LL(1), -x, conversión bin→mem | No construir CFG; rechazar la combinación con --emit-cfg. |
| --emit-cfg-opt | Pendiente de optimización; no implementarlo como copia del grafo inicial. |

DOT contiene subgrafos por rutina, nodos ENTRY/EXIT y bloques con el TAC/comentarios de flags del mismo IR. La etiqueta de rutina incluye firma y cabeceras de tipos. Usar identificadores como `r0_b0` para evitar colisiones entre rutinas; marcar nodos inalcanzables. Escapar comillas, barras invertidas y saltos de línea. Graphviz no es dependencia para construir el compilador ni escribir DOT.

Ampliar las validaciones de opciones y colisiones existentes, incluyendo rutas equivalentes/enlaces. Reutilizar el mecanismo de temporal y renombrado para DOT y preservar archivos anteriores ante fallo. No agregar un segundo frontend ni un sistema distinto de publicación. Limpieza: CFG → IR → AST → tabla.

La opción --emit-cfg está implementada (comando 34 de [comandos.md](../comandos.md)). La salida real se ilustra en [ejemplo-flujo-IR.md](ejemplo-flujo-IR.md).

### C4/C5 implementados: formato y publicación

| Elemento | Regla concreta |
| --- | --- |
| `IrPrintView` | Vista interna prestada a IR/rutina, con índice local de temporales/constantes. Liberar `values` al terminar. No se duplica IR ni se amplía su API pública. |
| `ir_print_prepare` | Reunir tipos/constantes una vez por rutina para ambos exportadores. |
| `ir_print_header` / `ir_print_block` | Imprimir la misma cabecera/TAC que ir_write; DOT usa estas funciones directamente desde estructuras. |
| `cfg_write_dot` | Un subgrafo por rutina y nodos/aristas con prefijo de rutina. ENTRY/EXIT ovalados; nodos inalcanzables en gris discontinuo. |
| `write_label` | Buffer local solo para escapar comillas, barras invertidas y saltos de línea de una etiqueta. No se analiza como IR. Se libera también al fallar. |
| main | Generar IR una sola vez si se pide IR o CFG; construir grafo solo para --emit-cfg. Liberar CFG antes de IR y tabla. |
| `write_analysis` | Compartir publicación atómica entre IR/DOT, con temporal junto al destino y renombrado tras escritura/cierre correctos. No se agrega otro sistema de archivos. |
| Makefile | Enlazar CFG solo en pipoc y declarar dependencias del formato compartido; porygon-schedule mantiene sus fuentes anteriores. |

Se comprobaron seis fuentes, igualdad del .ir previo al refactor, DOT determinista aceptado por Graphviz, binario sin cambios, combinaciones CLI incompatibles, colisiones/enlaces, preservación de archivos y limpieza tras fallos. Además se revisaron escape de etiquetas, ramas con destino compartido, estilo de inalcanzables y propagación de errores de stream con comprobaciones temporales.

## 8. Casos de revisión manual

| Caso | Entrada pequeña | Qué comprobar |
| --- | --- | --- |
| CFG-T1 | `tests/ir/scalar.rwt` | ENTRY → B0 → EXIT. |
| CFG-T2 | `tests/ir/branches.rwt` | TRUE/FALSE y unión de ambas ramas, sin plegar condición constante. |
| CFG-T3 | `tests/ir/loop_break.rwt` | Vuelta a condición y salida correcta de break. |
| CFG-T4 | Fuente con dos rutinas y una llamada, más un caso interno preparado con bloque inalcanzable. | Grafos separados; ninguna arista de llamada; marcas de alcance correctas. Variante con destino inexistente debe fallar. |
| CFG-T5 | IR interna preparada: BRANCH con ambos destinos al mismo bloque. | Dos aristas etiquetadas, un vecino y DOT estable al exportar dos veces. |

Dejar fuentes y archivos esperados para ejecutar desde terminal y comparar aristas/DOT como texto. No añadir runner Python ni pruebas de imágenes. Los casos internos T4/T5 requieren entradas preparadas porque el generador actual evita parte del código inalcanzable y no suele emitir una rama con destinos iguales; una fuente Piper normal no garantiza esas formas de IR.

Los casos internos se verificaron de forma temporal durante el desarrollo. Las fuentes y seis DOT esperados están en tests/cfg y tests/ir; matrices/Feistel comprueban etiquetas, sin nuevas reglas de conexiones. Comandos de exportación, comparación y visualización: [tests/cfg/README.md](../../tests/cfg/README.md). Sin runner permanente.
