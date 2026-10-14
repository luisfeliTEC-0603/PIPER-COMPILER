# PIPER COMPILER 

<div align="center">
  <img src="docs/imgs/rowlet.png" alt="rowlet" width="20%" height="auto">

  [![Compilador](https://img.shields.io/badge/Compilador-C-ecc6a9?style=for-the-badge&logoColor=white)]()
  [![Arquitectura](https://img.shields.io/badge/Arquitectura-PORYGON-1eae89?style=for-the-badge&logoColor=white)]()
  [![Lenguaje](https://img.shields.io/badge/Lenguaje-Piper-ecc6a9?style=for-the-badge&logoColor=white)]()

  **Compilador en C que traduce el lenguaje `Piper` a binario —específico para la arquitectura `PORYGON`**
</div>

## Salida del compilador

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

## Ejecución integrada en PORYGON

Desde la raíz del compilador, un solo comando compila Piper, genera la memoria
de instrucciones y ejecuta el procesador:

```sh
./run.sh tests/codegen/call_simple.rwt 1000 \
  --expect r1=10 --expect r7=0x20000 --expect errors=0
```

El presupuesto predeterminado es 200 ciclos. `--data archivo.mem` inicializa la
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

El código C del ensamblador está en [`porygon_assembler/`](porygon_assembler/Flujo_assembler.md).
El `Makefile` lo compila y enlaza con `pipoc`. Su parser recibe ASM PORYGON
lineal, escrito a mano o generado desde el AST, y construye el `MachineProgram`
que consume el scheduler. `bin/porygon-schedule entrada.asm salida.bundles [salida.mem]`
genera bundles con selección de ALU y, opcionalmente, la imagen hexadecimal
para el hardware. Este pipeline también está integrado en `pipoc` mediante los
comandos `sch`, `mem` y `all`.

El formato, las reglas temporales, la API y las pruebas están en
[`docs/scheduler.md`](docs/scheduler.md). Ejecutar `make test-scheduler` para
comprobar el calendario y su verificador independiente.
