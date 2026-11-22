# Comandos
Resumen de comandos ejecutables para PIPER-COMPILER

| N.º | Comando | Descripción | Estado |
| --- | --- | --- | --- |
| 1 | `make all` | Construye `bin/pipoc` y `bin/porygon-schedule`. | Implementado |
| 2 | `make generate` | Genera los archivos de Flex, Bison y la tabla LL(1). | Implementado |
| 3 | `make clean` | Elimina `build/` y `bin/`, incluidos los artefactos guardados allí. | Implementado |
| 4 | `bin/pipoc --help` | Muestra los comandos y opciones disponibles. | Implementado |
| 5 | `bin/pipoc programa.rwt` | Compila el programa y genera `programa.bin`; actualmente no genera `.mem` automáticamente. | Implementado |
| 6 | `bin/pipoc programa.rwt -o salida.bin` | Cambia la ruta del binario generado. | Implementado |
| 7 | `bin/pipoc programa.rwt --emit-mem programa.mem` | Genera el binario y la imagen hexadecimal para simular en PORYGON. | Implementado |
| 8 | `bin/pipoc asm programa.rwt -o salida.asm` | Exporta únicamente el ASM lineal, antes de la calendarización. | Implementado |
| 9 | `bin/pipoc sch programa.rwt -o salida.bundles` | Exporta los bundles calendarizados, con instrucciones y NOPs por slot. | Implementado |
| 10 | `bin/pipoc mem programa.rwt -o salida.mem` | Exporta únicamente la imagen hexadecimal compatible con `$readmemh`. | Implementado |
| 11 | `bin/pipoc ins programa.rwt` | Exporta `programa.ast.json` y `programa.symbols.txt`. | Implementado |
| 12 | `bin/pipoc all programa.rwt` | Exporta ejecutable, AST/símbolos, tres IR, tres CFG, interferencias IR y asignación de registros. Con DCE añade `.dce.json`. `-o carpeta/nombre.bin` reúne todas las salidas bajo esa carpeta y prefijo. | Implementado |
| 13 | `bin/pipoc programa.rwt -s` | Conserva el ASM en `programa.asm`, además del binario. | Implementado |
| 14 | `bin/pipoc programa.rwt -v` | Muestra las fases de compilación en la salida de diagnóstico. | Implementado |
| 15 | `bin/pipoc programa.rwt -t` | Imprime el AST legible, además de generar el binario. | Implementado |
| 16 | `bin/pipoc programa.rwt -m` | Imprime la tabla de símbolos y la distribución de memoria, además de generar el binario. | Implementado |
| 17 | `bin/pipoc programa.rwt -p` | Imprime los bundles en formato numérico y con mnemónicos, además de generar el binario. | Implementado |
| 18 | `bin/pipoc programa.rwt --ast-json salida.ast.json` | Exporta el AST en JSON, además del binario. | Implementado |
| 19 | `bin/pipoc programa.rwt --emit-symbols salida.symbols.txt` | Exporta símbolos y distribución de memoria, además del binario. | Implementado |
| 20 | `bin/pipoc programa.rwt --emit-asm salida.asm` | Exporta el ASM lineal en la ruta indicada, además del binario. | Implementado |
| 21 | `bin/pipoc programa.rwt --emit-bundles salida.bundles` | Exporta la calendarización final en la ruta indicada, además del binario. | Implementado |
| 22 | `bin/pipoc --check programa.rwt` | Comprueba léxico, sintaxis y semántica sin generar artefactos. | Implementado |
| 23 | `bin/pipoc --check --diagnostics-json programa.rwt` | Comprueba el programa y emite diagnósticos JSON por stdout. | Implementado |
| 24 | `bin/pipoc --ll1` | Imprime las celdas definidas de la tabla predictiva LL(1). | Implementado |
| 25 | `bin/pipoc --ll1-expected prefijo.rwt` | Lista los terminales válidos después de un prefijo Piper. | Implementado |
| 26 | `bin/pipoc --clones 8 --clones-exact programa.rwt` | Reporta clones Tipo I dentro del archivo, con mínimo de ocho nodos. | Implementado |
| 27 | `bin/pipoc --clones 8 --clones-renamed programa.rwt` | Reporta clones con renombramiento de identificadores dentro del archivo, con mínimo de ocho nodos. | Implementado |
| 28 | `bin/pipoc -x entrada.asm -o salida.bin` | Ensambla, calendariza y genera binario desde ASM sin ejecutar el frontend Piper. | Implementado |
| 29 | `bin/pipoc --bin-to-mem programa.bin -o salida.mem` | Convierte un ejecutable binario validado a imagen hexadecimal. | Implementado |
| 30 | `bin/pipoc --disasm-bin programa.bin -o salida.mem` | Alias de `--bin-to-mem`; produce `.mem`, no ASM con mnemónicos. | Implementado |
| 31 | `bin/porygon-schedule entrada.asm salida.bundles salida.mem` | Calendariza ASM y exporta bundles e imagen hexadecimal; el argumento `salida.mem` es opcional. | Implementado |
| 32 | `bin/pipoc ir programa.rwt -o salida.ir` | Genera únicamente IR tipada de tres direcciones y bloques básicos; sin `-o`, guarda `programa.ir` junto al fuente. | Implementado |
| 33 | `bin/pipoc programa.rwt --emit-ir salida.ir` | Exporta IR original con tipos, bloques y efectos, antes de unroll/DCE; la IR activa final alimenta codeOPgen mediante regalloc. | Implementado |
| 34 | `bin/pipoc programa.rwt --emit-cfg salida.dot` | Exporta CFG en DOT con TAC, tipos, efectos y ramas por rutina, además de la salida principal; también se combina con el comando ir. | Implementado |
| 34a | `bin/pipoc ir programa.rwt -o programa.ir --emit-regalloc-graph interferencia.dot` | Exporta el grafo preliminar de interferencia de temporales IR, con un subgrafo por rutina. No representa todavía la asignación física. | Implementado |
| 35 | `bin/pipoc programa.rwt -O0` | Convención opcional propuesta, no exigida por el enunciado: desactiva optimizaciones opcionales para obtener una referencia; conserva la asignación de registros, el spilling necesario y la calendarización correcta. | Pendiente |
| 36 | `bin/pipoc programa.rwt -O1` | Convención opcional propuesta, no exigida por el enunciado: agrupa DCE, unrolling con heurística y reordenamiento seguro; esta agrupación aún no está definida en la CLI. | Pendiente |
| 37 | `bin/pipoc ir programa.rwt --dce` | Elimina rutinas inalcanzables desde el arranque o `main` por llamadas directas; luego aplica DCE con liveness sobre temporales y locales seguros. Conserva las llamadas de las rutinas alcanzables sin analizar sus efectos. Exporta IR original y `programa.opt.ir`. La IR activa alimenta codeOPgen; tambien se admite `--dce` al producir ASM/binario/memoria. | Implementado |
| 38 | `bin/pipoc programa.rwt --unroll 4` | Desenrolla donde sea valido, con resto; el resultado alimenta asignacion y codeOPgen. | Implementado |
| 39 | `bin/pipoc programa.rwt --unroll auto` | Selecciona factor mediante heuristica y presupuesto; `--unroll off` omite la pasada. | Implementado |
| 39a | `bin/pipoc programa.rwt --spill off` | Prohibe derrames; si faltan registros, falla e indica habilitar «Permitir spilling». Por defecto `--spill on`. Las otras optimizaciones siguen independientes. | Implementado |
| 39b | `bin/pipoc programa.rwt --rename` | Recolorea temporales de un solo bloque después de asignar registros y resolver los spills. Busca eliminar WAR y WAW entre temporales distintos. Está desactivado por defecto y se puede combinar con `--dce`, `--unroll` y `--spill`. | Implementado |
| 40 | `bin/pipoc programa.rwt --reorder` | Propuesto: selecciona reordenamiento seguro, exigido por el enunciado. El scheduler ya reordena: reutilizarlo no exige reescribir su algoritmo; esta bandera aún no existe. | Pendiente |
| 41 | `bin/pipoc ir programa.rwt --dce --emit-ir-opt salida.opt.ir` | Exporta la IR activa final tras las pasadas seleccionadas. `--emit-ir` exporta la original, antes de unroll/DCE. Sin pasadas, las dos IR son iguales. | Implementado |
| 42 | `bin/pipoc ir programa.rwt --dce --emit-cfg-opt salida.opt.dot` | Exporta CFG de las rutinas conservadas con instrucciones optimizadas; la poda reconstruye el CFG y la DCE por liveness conserva bloques y terminadores. | Implementado |
| 43 | `bin/pipoc ir programa.rwt --dce --emit-opt-report optimizaciones.json` | Exporta conteos IR antes/despues, instrucciones y rutinas eliminadas, y tiempo CPU de ambas pasadas DCE (incluida la reconstruccion de CFG) y de compilacion a IR. El conteo de instrucciones eliminadas incluye los cuerpos de las rutinas eliminadas. Bytes ISA y ciclos del simulador quedan en null hasta implementar su recoleccion; el backend ya consume la IR activa. | Implementado con metricas ISA pendientes |
| 44 | `bin/pipoc programa.rwt --emit-regalloc registros.txt` | Exporta operaciones de máquina finales, registros, spills y ubicaciones fuente/IR. Con `--rename` incluye rutina, bloque, temporal, registro anterior, registro nuevo y el total de operaciones afectadas. | Implementado |
| 45 | `bin/pipoc all programa.rwt --unroll auto --dce --spill on -o build/pruebas/programa.bin` | Reúne el ejecutable con las IR y CFG de cada etapa, interferencias IR, reporte DCE y asignación de registros bajo build/pruebas (crear la carpeta primero). El IDE añade su reporte, log y ZIP. | Implementado |
| 46 | `make -C ../PORYGON sim PROG=../PIPER-COMPILER/programa.mem MAX_CYCLES=200` | Simula la imagen en PORYGON con límite de 200 ciclos; guarda volcado de memoria, ondas VCD, resultados y logs en `../PORYGON/build/sim/programa/`. | Implementado |
| 47 | `../PORYGON/tools/sim.sh programa.mem --data datos.mem --max-cycles 1000 --out simulacion --expect r1=14 --expect errors=0` | Simula con memoria inicial y directorio explícitos; verifica el valor de retorno y el contador de errores. | Implementado |
| 48 | `../PORYGON/tools/memory/extract.sh programa 0x10000 32 resultado.bin` | Extrae 32 bytes desde la dirección indicada del volcado de la simulación y guarda el binario junto a ese volcado. | Implementado |
| 49 | `python3 -m venv ide/.venv` | Crea el entorno virtual de Python para el IDE desde la raíz del repositorio. | Implementado |
| 50 | `ide/.venv/bin/python -m pip install -e ./ide` | Instala el IDE y sus dependencias en el entorno virtual; requiere Python 3.11 o superior. | Implementado |
| 51 | `ide/.venv/bin/python -m piper_ide` | Abre el IDE usando el entorno virtual preparado. | Implementado |
| 52 | `bin/pipoc ir programa.rwt --unroll auto --emit-ir-unroll tras-unrolling.ir` | Captura la IR después de unrolling y antes de DCE, en el mismo recorrido del compilador. | Implementado |
| 53 | `bin/pipoc ir programa.rwt --emit-cfg-original original.dot` | Exporta el CFG original antes de las pasadas. `--emit-cfg` exporta el CFG después de unrolling y antes de DCE. | Implementado |

## Comparar una asignación con y sin renombramiento

Desde la raíz de Piper podés generar las dos variantes del mismo fuente y seguir sus operandos hasta los bundles.

```bash
make all
mkdir -p build/runs/rename/war
bin/pipoc all tests/rename/war.rwt --unroll off --spill off -o build/runs/rename/war/base.bin
bin/pipoc all tests/rename/war.rwt --unroll off --spill off --rename -o build/runs/rename/war/rename.bin
```

Una línea como `[rename] main B0 t2 r1 -> r3` indica que todas las definiciones y lecturas de `t2` pasaron a `r3`. El campo `operaciones_de_maquina_afectadas` cuenta **operaciones virtuales estáticas de la máquina asignada**, después de insertar spills y antes de emitir ASM. Una operación suma uno si cambió cualquiera de sus operandos, aunque cambien dos o el destino sea también lectura. El emisor puede expandir una operación en varias instrucciones físicas u omitir una copia. Los `.bundles` contienen las instrucciones físicas efectivamente emitidas.

`make check-rename` prueba el asignador y compara reporte, ASM, bundles e imagen. `make check-rename-sim` además ejecuta ambas versiones de cuatro programas con el simulador de la carpeta hermana `../PORYGON`. Guarda la comparación en `build/runs/rename/simulacion.tsv` y los `results.txt` e `indicators.json` por caso y variante. [Programas y comprobaciones](../tests/rename)
