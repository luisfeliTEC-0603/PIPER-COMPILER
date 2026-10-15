# PIPER COMPILER 

<div align="center">
  <img src="docs/imgs/rowlet.png" alt="rowlet" width="20%" height="auto">

  [![Compilador](https://img.shields.io/badge/Compilador-C-ecc6a9?style=for-the-badge&logoColor=white)]()
  [![Arquitectura](https://img.shields.io/badge/Arquitectura-PORYGON-1eae89?style=for-the-badge&logoColor=white)]()
  [![Lenguaje](https://img.shields.io/badge/Lenguaje-Piper-ecc6a9?style=for-the-badge&logoColor=white)]()

  **Compilador en C que traduce el lenguaje `Piper` a binario —específico para la arquitectura `PORYGON`**
</div>

**Ir a:** [Directorios](#directorios-del-repositorio) ·
[Uso del compilador](#salida-del-compilador) ·
[Ejemplos y salidas](#ejemplos-incluidos) ·
[Ejecución en PORYGON](#ejecución-integrada-en-porygon) ·
[Ensamblador](#ensamblador-porygon)

## Directorios del repositorio

| Ruta | Qué encontrarás |
| --- | --- |
| [`src/`](src/) | CLI, lexer, parser, AST, análisis semántico, tabla de símbolos y formato binario. |
| [`src/codegen/`](src/codegen/) | Generación de ASM: expresiones, control de flujo, llamadas, arreglos e intrínsecas. |
| [`include/`](include/) | Encabezados y contratos compartidos del compilador. |
| [`porygon_assembler/`](porygon_assembler/) | Lectura del ASM y codificación de instrucciones PORYGON. |
| [`scheduler/`](scheduler/) | Organización de instrucciones en bundles, verificación y emisión de salidas. |
| [`tools/`](tools/) | Punto de entrada de la herramienta de calendarización independiente. |
| [`tests/codegen/`](tests/codegen/) | Los tres programas Piper de ejemplo (`.rwt`). |
| [`tests/codegen/generated/`](tests/codegen/generated/) | ASM lineal (`.asm`) y memoria de instrucciones (`.mem`) de cada ejemplo. |
| [`docs/`](docs/) | PDF de [especificaciones](docs/specs.pdf), [sintaxis Piper](docs/piper_sintaxis.pdf) e imágenes. |
| [`Makefile`](Makefile) | Reglas para compilar con `make all` y limpiar con `make clean`. |

Para seguir el flujo del compilador, empezar en [`src/main.c`](src/main.c):
coordina el [lexer](src/lexer.l), el [parser](src/parser.y), la
[semántica](src/semantic.c) y la [generación de código](src/codegen/codegen.c),
antes de ensamblar, calendarizar y emitir el binario.

`build/` y `bin/` se crean localmente durante la compilación y están ignorados
por Git. `make clean` elimina ambas carpetas, incluidos los resultados en
`build/runs/`.

## Salida del compilador

### Extensión de VS Code

Hay una [extensión local de Piper](editors/vscode/README.md) con colores para
`.rwt`, snippets y comandos de compilación con errores en el panel Problemas.
Para empaquetarla e instalarla desde la raíz del repositorio:

```sh
python3 editors/vscode/package.py
code --install-extension build/vscode-extension/piper-language-0.1.0.vsix
```

Para compilar desde el editor, ejecutar primero `make all`. La extensión busca
`bin/pipoc` en el proyecto o `pipoc` en PATH y guarda las salidas en
`build/vscode/`. También se puede configurar `piper.compilerPath`.

Todo programa debe declarar una rutina global `main[]`, sin parámetros.
Por ejemplo: `routine B32 main[] { output 0. }`.
El analizador semántico rechaza archivos sin esa entrada, incluso si contienen
sentencias globales. Los fixtures que solo declaran rutinas necesitan agregar
`main[]` para pasar el análisis semántico.

La invocación mínima ejecuta el pipeline completo y genera `archivo.bin`:

```sh
bin/pipoc archivo.rwt
```

Para pedir un artefacto concreto existen cinco comandos simples:

| Comando | Archivos generados |
| --- | --- |
| `bin/pipoc asm archivo.rwt` | `archivo.asm`, sin calendarizar |
| `bin/pipoc sch archivo.rwt` | `archivo.bundles`, calendarizado por slots |
| `bin/pipoc mem archivo.rwt` | `archivo.mem`, compatible con `$readmemh` |
| `bin/pipoc ins archivo.rwt` | `archivo.ast.json` y `archivo.symbols.txt` |
| `bin/pipoc all archivo.rwt` | Todos los anteriores y `archivo.bin` |

En `asm`, `sch` y `mem`, `-o` cambia la ruta del único artefacto. `ins` acepta
`--ast-json` y `--emit-symbols` porque produce dos archivos. En `all`, `-o`
solo cambia la ruta del `.bin`; las demás rutas pueden controlarse con
`--emit-asm`, `--emit-bundles`, `--emit-mem`, `--ast-json` y
`--emit-symbols`.

Las opciones de diagnóstico `-t`, `-m`, `-p` y `-v` continúan disponibles, al
igual que la interfaz avanzada mostrada por `bin/pipoc --help`.

## Ejemplos incluidos

| Ejemplo | Fuente Piper | ASM generado | Memoria de instrucciones |
| --- | --- | --- | --- |
| Factorial | [factorial.rwt](tests/codegen/factorial.rwt) | [factorial.asm](tests/codegen/generated/factorial.asm) | [factorial.mem](tests/codegen/generated/factorial.mem) |
| Búsqueda en matriz | [busqueda_matriz.rwt](tests/codegen/busqueda_matriz.rwt) | [busqueda_matriz.asm](tests/codegen/generated/busqueda_matriz.asm) | [busqueda_matriz.mem](tests/codegen/generated/busqueda_matriz.mem) |
| Autenticación y cuatro rondas Feistel | [crypto_feistel_pipeline.rwt](tests/codegen/crypto_feistel_pipeline.rwt) | [crypto_feistel_pipeline.asm](tests/codegen/generated/crypto_feistel_pipeline.asm) | [crypto_feistel_pipeline.mem](tests/codegen/generated/crypto_feistel_pipeline.mem) |

Sus archivos `.asm` y `.mem` generados se conservan en
`tests/codegen/generated/`. Esta carpeta no está ignorada por Git y se conserva
al ejecutar `make clean`. Para regenerarlos desde la raíz del repositorio:

```sh
make all
mkdir -p tests/codegen/generated
for programa in factorial busqueda_matriz crypto_feistel_pipeline; do
  bin/pipoc mem -o "tests/codegen/generated/$programa.mem" \
    --emit-asm "tests/codegen/generated/$programa.asm" \
    "tests/codegen/$programa.rwt"
done
```

## Ejecución integrada en PORYGON

Desde la raíz del compilador, un solo comando compila Piper, genera la memoria
de instrucciones y ejecuta el procesador:

El ejemplo `call_simple.rwt` se conserva fuera del repositorio. Para usarlo en
esta sección, definir `PIPER_TESTS_DIR` con la ruta a la carpeta externa de pruebas.

```sh
./run.sh "$PIPER_TESTS_DIR/codegen/call_simple.rwt" 1000 \
  --expect r1=10 --expect r7=0x20000 --expect errors=0
```

 `--data archivo.mem` inicializa la
RAM de datos y `--expect clave=valor` valida registros `r0`–`r7`, `pc` o los
contadores presentes en `results.txt`. Los artefactos quedan en
`build/runs/<programa>/`. El runner detecta PORYGON en la carpeta hermana
esperada; `PORYGON_DIR=/otra/ruta/PORYGON` permite cambiarla sin editar scripts.

La extracción pertenece a PORYGON. Después de una simulación creada con
`make sim`, basta indicar el programa, la dirección, el tamaño y el archivo de
salida:

```sh
cd ../../ProyectoArqui/PORYGON
./extract.sh programa 0x10000 32 salida.bin
```

El wrapper lee `sim_output/programa/memory.dump.mem` y guarda la salida junto al
dump. El primer argumento también puede ser una ruta explícita a cualquier dump
`.mem`.

## Ensamblador PORYGON

El código C del ensamblador está en [`porygon_assembler/`](porygon_assembler/).
El `Makefile` lo compila y enlaza con `pipoc`. Su parser recibe ASM PORYGON
lineal, escrito a mano o generado desde el AST, y construye el `MachineProgram`
que consume el scheduler. `bin/porygon-schedule entrada.asm salida.bundles [salida.mem]`
genera bundles con selección de ALU y, opcionalmente, la imagen hexadecimal
para el hardware. Este pipeline también está integrado en `pipoc` mediante los
comandos `sch`, `mem` y `all`.

Los demás casos de prueba se conservan fuera del repositorio.
