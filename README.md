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
[Ejecución en PORYGON](#ejecución-integrada-en-porygon) ·
[Flujo completo](#flujo-completo)

## Directorios del repositorio

| Ruta | Qué encontrarás |
| --- | --- |
| [`src/`](src/) | CLI, lexer, parser, AST, análisis semántico, tabla de símbolos y formato binario. |
| [`src/codegen/`](src/codegen/) | Generación de ASM: expresiones, control de flujo, llamadas, arreglos e intrínsecas. |
| [`include/`](include/) | Encabezados y contratos compartidos del compilador. |
| [`porygon_assembler/`](porygon_assembler/) | Lectura del ASM y codificación de instrucciones PORYGON. |
| [`scheduler/`](scheduler/) | Organización de instrucciones en bundles, verificación y emisión de salidas. |
| [`tools/`](tools/) | Herramientas auxiliares, incluido el generador de la tabla LL(1). |
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

### Gramática predictiva LL(1)

La gramática [piper.ll1](src/analysis/ll1/piper.ll1) genera FIRST, FOLLOW y la
tabla predictiva durante `make`. Los archivos quedan en `build/analysis/ll1/`.
Para consultar la tabla: `bin/pipoc --ll1` (comando 24 de [comandos.md](docs/comandos.md)).
El parser Bison construye el AST; LL(1) también permite consultar terminales esperados.

## Salida del compilador

Para instalar y abrir el IDE desde la raíz del repositorio (Python 3.11 o superior):

```sh
python3 -m venv ide/.venv
ide/.venv/bin/python -m pip install -e ./ide
ide/.venv/bin/python -m piper_ide
```

La creación del entorno y la instalación se hacen una vez; el comando 51 abre el IDE
(comandos 49–51 en la tabla).
Ejecutar `make all` antes de compilar desde el IDE.

Las salidas disponibles, incluida IR, y las opciones futuras de CFG y optimización están
numeradas en [comandos.md](docs/comandos.md), con su estado de implementación.

## Ejecución integrada en PORYGON

Desde la raíz de PIPER-COMPILER, con PORYGON en la carpeta hermana. El archivo
`programa.rwt` debe declarar una rutina global `main[]` sin parámetros.
Se requieren GCC, Flex, Bison, Python 3 e Icarus Verilog (`iverilog` y `vvp`).

```sh
# 1. Construir el compilador (comando 1).
make all

# 2. Compilación mínima: genera programa.bin (comando 5).
bin/pipoc programa.rwt

# 3. Convertir el binario a memoria de instrucciones (comando 29).
bin/pipoc --bin-to-mem programa.bin -o programa.mem

# 4. Ejecutar el procesador, con límite de 1000 ciclos (comando 46).
make -C ../PORYGON sim PROG=../PIPER-COMPILER/programa.mem MAX_CYCLES=1000

# 5. Extraer 32 bytes de memoria de datos desde 0x10000 (comando 48).
../PORYGON/tools/memory/extract.sh programa 0x10000 32 resultado.bin
```

La simulación guarda `results.txt`, `memory.dump.mem`, `waves.vcd` y logs en
`../PORYGON/build/sim/programa/`; la extracción guarda `resultado.bin` allí.
Ajustar dirección y tamaño al dato que se quiera recuperar.

## Flujo completo

Piper pasa por lexer, parser Bison, AST y análisis semántico. El codegen actual
traduce el AST a ASM; el [ensamblador](src/assembler/) construye el programa
máquina y el [scheduler](src/scheduler/) reordena y empaqueta instrucciones en
bundles con NOPs. Se emiten `.bin` y, al solicitarlo, `.mem` para ejecutar en
PORYGON y recuperar resultados de registros y memoria.

La IR tipada con bloques se exporta con `bin/pipoc ir programa.rwt` o
`bin/pipoc programa.rwt --emit-ir programa.ir` (comandos 32–33). El backend
actual sigue usando AST y símbolos; la optimización sigue pendiente.
Para añadir el grafo: `--emit-cfg programa.dot` (comando 34), también con `pipoc ir`.

Para revisar las cinco pruebas de IR, generar sus archivos desde terminal.
Fuentes, resultados esperados e instrucciones: [tests/ir/README.md](tests/ir/README.md).
Guías: [IR](docs/IR/todoIR.md) y [CFG](docs/IR/todoCFG.md).

Ejemplos, DOT de referencia y comandos para dibujarlos: [tests/cfg/README.md](tests/cfg/README.md).
