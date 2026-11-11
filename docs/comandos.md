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
| 12 | `bin/pipoc all programa.rwt` | Exporta `.bin`, `.asm`, `.bundles`, `.mem`, `.ast.json` y `.symbols.txt`; añade IR y CFG solo al solicitar `--emit-ir` y `--emit-cfg`. | Implementado |
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
| 33 | `bin/pipoc programa.rwt --emit-ir salida.ir` | Exporta IR sin optimizar con tipos, bloques y efectos, además de la salida principal; el backend actual sigue recibiendo AST y símbolos. | Implementado |
| 34 | `bin/pipoc programa.rwt --emit-cfg salida.dot` | Exporta CFG en DOT con TAC, tipos, efectos y ramas por rutina, además de la salida principal; también se combina con el comando ir. | Implementado |
| 35 | `bin/pipoc programa.rwt -O0` | Convención opcional propuesta, no exigida por el enunciado: desactiva optimizaciones opcionales para obtener una referencia; conserva la asignación de registros, el spilling necesario y la calendarización correcta. | Pendiente |
| 36 | `bin/pipoc programa.rwt -O1` | Convención opcional propuesta, no exigida por el enunciado: agrupa DCE, unrolling con heurística y reordenamiento seguro; esta agrupación aún no está definida en la CLI. | Pendiente |
| 37 | `bin/pipoc programa.rwt --dce` | Propuesto: activa eliminación de código muerto y genera `programa.bin`; para inspeccionar el resultado, combinar con `--emit-ir-opt salida.opt.ir` o `--emit-asm salida.asm`. | Pendiente |
| 38 | `bin/pipoc programa.rwt --unroll-factor 4` | Propuesto: solicita unrolling de factor cuatro donde sea válido, con manejo de iteraciones restantes. | Pendiente |
| 39 | `bin/pipoc programa.rwt --unroll-auto` | Propuesto: selecciona mediante heurística qué ciclos desenrollar y con qué factor. | Pendiente |
| 40 | `bin/pipoc programa.rwt --reorder` | Propuesto: selecciona reordenamiento seguro, exigido por el enunciado. El scheduler ya reordena: reutilizarlo no exige reescribir su algoritmo; esta bandera aún no existe. | Pendiente |
| 41 | `bin/pipoc programa.rwt -O1 --emit-ir-opt salida.opt.ir` | Propuesto: exporta la IR después de las optimizaciones seleccionadas, antes de traducirla a instrucciones PORYGON. | Pendiente |
| 42 | `bin/pipoc programa.rwt -O1 --emit-cfg-opt salida.opt.dot` | Propuesto: exporta el CFG actualizado después de las optimizaciones en texto DOT. `.opt` solo identifica la versión optimizada; el nombre del archivo es libre. | Pendiente |
| 43 | `bin/pipoc programa.rwt -O1 --emit-opt-report optimizaciones.json` | Propuesto: exporta pases aplicados, factores de unrolling, motivos para omitir transformaciones y conteos de instrucciones y bloques antes/después. | Pendiente |
| 44 | `bin/pipoc programa.rwt --emit-regalloc registros.txt` | Propuesto, opcional para depuración: exporta registros, offsets de pila relativos al frame e intervalos de validez por temporal, incluidos stores/loads por spilling; no registra valores durante la ejecución ni garantiza direcciones absolutas fijas. | Pendiente |
| 45 | `bin/pipoc all programa.rwt -O1 --emit-ir programa.ir --emit-cfg programa.dot --emit-ir-opt programa.opt.ir --emit-cfg-opt programa.opt.dot --emit-opt-report optimizaciones.json --emit-regalloc registros.txt` | Propuesto: reúne las salidas actuales con IR y CFG antes/después, informe de optimización y asignación de registros para revisar el flujo completo. | Pendiente |
| 46 | `make -C ../PORYGON sim PROG=../PIPER-COMPILER/programa.mem MAX_CYCLES=200` | Simula la imagen en PORYGON con límite de 200 ciclos; guarda volcado de memoria, ondas VCD, resultados y logs en `../PORYGON/build/sim/programa/`. | Implementado |
| 47 | `../PORYGON/tools/sim.sh programa.mem --data datos.mem --max-cycles 1000 --out simulacion --expect r1=14 --expect errors=0` | Simula con memoria inicial y directorio explícitos; verifica el valor de retorno y el contador de errores. | Implementado |
| 48 | `../PORYGON/tools/memory/extract.sh programa 0x10000 32 resultado.bin` | Extrae 32 bytes desde la dirección indicada del volcado de la simulación y guarda el binario junto a ese volcado. | Implementado |
| 49 | `python3 -m venv ide/.venv` | Crea el entorno virtual de Python para el IDE desde la raíz del repositorio. | Implementado |
| 50 | `ide/.venv/bin/python -m pip install -e ./ide` | Instala el IDE y sus dependencias en el entorno virtual; requiere Python 3.11 o superior. | Implementado |
| 51 | `ide/.venv/bin/python -m piper_ide` | Abre el IDE usando el entorno virtual preparado. | Implementado |
