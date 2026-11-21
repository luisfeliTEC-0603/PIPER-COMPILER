# Compilar desde el IDE

Desde la raíz del repositorio:

```bash
make all
ide/.venv/bin/python -m piper_ide
```

Si falta el entorno: `python3 -m venv ide/.venv` y
`ide/.venv/bin/python -m pip install -e ./ide` (una vez).

1. Abrir la carpeta del proyecto y un archivo `.rwt`.
2. Seleccionar los controles junto a **Compilar**:

| Control | Efecto | Estado inicial |
| --- | --- | --- |
| Loop unrolling | El módulo valida cada loop y elige automáticamente su factor | Desmarcado |
| DCE | Elimina código muerto después de unrolling | Desmarcado |
| Permitir spilling | Autoriza cargas/stores cuando no alcanza la asignación de registros | Marcado |

3. Mantener **Todo** (salida inicial) para revisar todas las salidas y presionar **F5**.
4. Leer **Compilación**: decisiones, motivos de rechazo, conteos IR y
   asignación a memoria por rutina, con ubicaciones de los accesos.
5. Seleccionar un archivo y pulsar **Abrir archivo**, o hacer doble clic.
   Los generados se abren como texto de solo lectura. Para ubicar el binario o
   el ZIP, pulsar **Abrir carpeta de resultados**.

Las selecciones se conservan al cerrar y abrir el IDE. Durante una compilación
los controles quedan bloqueados. **AST y símbolos** solo analiza el código:
deshabilita las opciones sin borrar su selección.

El IDE envía `--unroll auto` al marcar la casilla y `--unroll off` al desmarcarla.
El módulo parte de factor 2 para 2–8 iteraciones y 4 para más de 8; lo reduce
si necesita respetar el presupuesto de tamaño. Los loops no elegibles se conservan
y el reporte explica el motivo. El factor elegido aparece en los resultados,
sin selector manual. La API y la CLI conservan el factor fijo para pruebas técnicas.

El scheduler se aplica siempre a binario, memoria y bundles. ASM solo produce
instrucciones lineales, sin calendarizar. No hay opción para apagar el scheduler.

Si se requiere memoria con spilling desmarcado, la compilación falla y pide
habilitar **Permitir spilling**. Se conservan las demás selecciones para reintentar.

## Archivos y reportes

Cada ejecución usa una carpeta propia bajo `build/ide/`, con el nombre del fuente
y un identificador. El panel muestra su ruta; **Consola** muestra el comando
copiable y la salida completa. Los intentos anteriores permanecen en Generados.
Un fallo limpia la lista de resultados actuales; los archivos parciales del
intento fallido no se anuncian como resultados válidos.

**Todo** genera los siguientes archivos en la misma carpeta y los reúne en
`<nombre>.results.zip`:

| Sufijo | Contenido |
| --- | --- |
| `.bin`, `.asm`, `.bundles`, `.mem` | Binario, ASM, paquetes y memoria del procesador |
| `.ast.json`, `.symbols.txt` | AST y tabla de símbolos |
| `.ir` | IR original, antes de unrolling y DCE |
| `.unroll.ir` | IR después de unrolling, antes de DCE |
| `.opt.ir` | IR después de DCE, recibida por regalloc |
| `.original.dot` | CFG original |
| `.unroll.dot` | CFG después de unrolling, antes de DCE |
| `.opt.dot` | CFG final después de DCE |
| `.interference.dot` | Interferencias de temporales de la IR final; grafo preliminar, sin colorear |
| `.regalloc.txt` | Operaciones de máquina después de spilling, registros asignados, slots y ubicaciones |
| `.opt.json` | Opciones, comando, decisiones y métricas reales de los módulos |
| `.dce.json` | Reporte del módulo DCE, solo si se seleccionó DCE |
| `.compile.log` | Comando, resumen y salida completa de esta compilación |
| `.results.zip` | Todos los archivos anteriores, sin incluir otros intentos |

Si una pasada está desactivada o no modifica el programa, su IR coincide con
la etapa anterior. Spilling no reescribe la IR tipada: reescribe las operaciones
de máquina; su resultado está en `.regalloc.txt` y luego en `.asm`.

Los `.dot` son grafos en formato Graphviz. Para obtener una imagen:

```bash
dot -Tsvg programa.opt.dot -o programa.opt.svg
```

El IDE conserva los archivos individuales para compararlos y crea el ZIP solo
tras completar la compilación. El paquete puede copiarse o adjuntarse a las pruebas.

El IDE construye `.opt.json` a partir de los mensajes de `pipoc -v`; no es el
JSON exclusivo de DCE que exporta `--emit-opt-report`. Cargas/stores e instrucciones
son conteos estáticos, no ciclos ni accesos medidos durante la ejecución.

## Exportar desde terminal

Para obtener todas las etapas en una sola carpeta:

```bash
mkdir -p build/pruebas/matriz
bin/pipoc all tests/codeOPgen/paso-a-paso/loop_matriz.rwt \
  -o build/pruebas/matriz/matriz.bin --unroll auto --dce --spill on -v
diff -u build/pruebas/matriz/matriz.ir build/pruebas/matriz/matriz.unroll.ir
diff -u build/pruebas/matriz/matriz.unroll.ir build/pruebas/matriz/matriz.opt.ir
```

Los `diff` muestran los cambios de cada pasada; código de salida 1 significa
que encontraron diferencias. `all -o carpeta/nombre.bin` usa esa carpeta y
prefijo para todas las salidas, salvo las rutas `--emit-*` indicadas explícitamente.
La terminal exporta los datos del compilador; `.opt.json`, `.compile.log` y el
ZIP los añade el IDE.

Para solicitar únicamente las IR, sin generar ejecutable:

```bash
bin/pipoc ir tests/codeOPgen/paso-a-paso/loop_matriz.rwt \
  -o build/pruebas/matriz/original.ir --unroll auto --dce \
  --emit-ir-unroll build/pruebas/matriz/tras-unrolling.ir \
  --emit-ir-opt build/pruebas/matriz/tras-dce.ir
```

Para comprobar los controles, usar `tests/codeOPgen/paso-a-paso/funcion_suma.rwt`
y `loop_matriz.rwt`. Sin pasadas, ambas IR coinciden; con DCE la función pasa de
18 a 17 instrucciones. Con unrolling automático (elige factor 2) y DCE, la matriz pasa de 34 a 51 y luego
a 50. `tests/codeOPgen/arguments.rwt` requiere spilling: desmarcarlo muestra
el diagnóstico; al marcarlo se generan los archivos. Los programas completos
y resultados de ejecución están en [las pruebas simples](codeOPgen-pruebas-simples.md).
