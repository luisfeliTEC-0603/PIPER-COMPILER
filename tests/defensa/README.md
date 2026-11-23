# Ejemplos de defensa: Python y Piper

Los cinco archivos originales están en `python/`. Sus traducciones ejecutables
están en `piper/`, con el mismo nombre y extensión `.rwt`. Cada `.rwt` tiene su
propio `main`: compilá un archivo por vez.

## Datos y resultados

| Archivo `.rwt` | Datos que prepara su `main` | Retorno esperado en r1 |
| --- | --- | --- |
| `LoopUnrolling` | Dos bloques: `[1,2,3,4]` y `[5,6,7,8]`; acumula cada valor por 2 | **72** |
| `Reordenamiento` | Lecturas `[20,60,80,40]`; acumula solo las mayores que 50 | **900** |
| `SpillingDeRegistros` | Argumentos s1..s8 = 1..8 | **774** |
| `pruebaEliminacionCodigo` | A=`[[1,2],[3,4]]`, B=`[[5,6],[7,8]]`; C=`[[19,22],[43,50]]` | **134**, suma de los cuatro elementos de C |
| `IdentificacionDeClones` | Promedio de ventas 25, temperaturas 22.5, gastos 25 y mediana de edades 23 | **9550**, suma de resultados en centésimas |

`output` devuelve un valor; no imprime texto. Para comprobar ejecución hay que
usar el simulador o el panel de mediciones con PORYGON configurado.

## Qué se adaptó del Python

- **Listas y `len`:** Piper usa arreglos con dimensiones declaradas. Estos ejemplos
  fijan matrices 2x2, bloques 2x4 y arreglos de cuatro elementos. No aceptan tamaños
  arbitrarios ni bloques de distinta longitud como podría hacer Python.
- **División decimal:** Piper no tiene `float`. Los promedios y la mediana se
  devuelven multiplicados por 100: `2250` significa `22.50`. La división entera
  trunca si hay más de dos decimales; no es una implementación general de los
  números decimales de Python. Las muestras elegidas se representan exactamente.
- **Arreglos de salida:** la multiplicación recibe `C` como parámetro, escribe sus
  elementos y devuelve el estado 0. `main` devuelve la suma de C para poder
  verificar el resultado escalar en r1.
- **`sorted`:** la mediana copia edades a un arreglo local y ordena la copia con
  bubble sort, conservando el arreglo original. El parámetro `n`, de 0 a 4,
  permite indicar cuántos elementos están activos, incluido el caso vacío.
- **Clones:** ventas y gastos usan el mismo nombre de parámetro, `valores`, para
  que sus cuerpos sean exactamente iguales y se pueda demostrar Tipo I. La
  versión de temperaturas cambia los nombres y permite demostrar Tipo II.
- **Código muerto:** se conservaron las asignaciones inútiles de los Python.
  DCE puede quitar sus stores, pero conserva cálculos que podrían fallar por
  overflow. No se debe esperar que desaparezca toda operación llamada `basura`.
- **Sintaxis:** la igualdad se escribe `=`, las asignaciones usan `store` y los
  accesos usan `matriz|fila&columna|`. Los índices calculados se guardan primero
  en variables. No se agregaron paréntesis de agrupación que la gramática no admite.

## Compilar y ejecutar desde terminal

Desde la raíz del repositorio:

```bash
make all
mkdir -p build/defensa

bin/pipoc all tests/defensa/piper/LoopUnrolling.rwt \
  -o build/defensa/LoopUnrolling.bin \
  --unroll auto --dce --rename --spill on \
  --emit-metrics build/defensa/LoopUnrolling.metrics.json -v
```

Se generan `.asm`, `.mem`, `.bundles`, tres IR por etapa, grafos, tabla de símbolos,
asignación de registros y reporte DCE. La CLI no prepara el ZIP del IDE.

Para ejecutar lo generado, con `iverilog`, `vvp` y el repositorio vecino PORYGON:

```bash
../PORYGON/tools/sim.sh build/defensa/LoopUnrolling.mem \
  --max-cycles 200000 --expect r1=72 --expect errors=0
```

Para otro ejemplo, reemplazá el nombre del archivo, el prefijo de salida y el
valor esperado según la tabla. Por ejemplo, `SpillingDeRegistros` espera `r1=774`.

El testbench actual tiene un límite de 128 bytes para rutas de archivos: usá
rutas de salida cortas como las anteriores. El comprobador incluido usa copias
`.mem` en rutas temporales cortas y guarda el resultado de cada simulación.

### Comprobar los cinco contra Python

```bash
# Compilar las versiones base y optimizada; todavía no ejecuta el RTL.
python3 tests/defensa/comprobar_traducciones.py

# Además, ejecutar las diez versiones y comprobar su resultado en PORYGON.
python3 tests/defensa/comprobar_traducciones.py --sim
```

El comprobador ejecuta las funciones Python con los mismos datos, adapta la salida
de matrices y centésimas como se explicó arriba y usa ese resultado como referencia
para el RTL. También verifica detección de clones I/II y rechazo de spilling
prohibido. Los originales `.py` solo definen funciones: ejecutarlos directamente
no imprime una demostración porque no contienen un `main` de prueba.

Los resultados quedan en:

```text
build/defensa-traducciones/
  resumen.json
  clones-tipo-I.json
  clones-tipo-II.json
  spill-prohibido.log
  <nombre>/base/...
  <nombre>/optimizado/...
```

Cada variante guarda `compilacion.log`, artefactos y, con `--sim`,
`simulacion.log` y `simulacion.results.txt`. La versión base tiene unroll apagado,
DCE/Rename apagados y spilling permitido; la optimizada activa unroll automático,
DCE y Rename, también permitiendo spills. El scheduler se aplica en ambas.

## Desde el IDE

Si el entorno ya está instalado:

```bash
ide/.venv/bin/python -m piper_ide
```

Si todavía no existe:

```bash
python3 -m venv ide/.venv
ide/.venv/bin/python -m pip install -e ./ide
ide/.venv/bin/python -m piper_ide
```

1. Abrí la carpeta raíz **PIPER-COMPILER**.
2. Abrí uno de los `.rwt` de `tests/defensa/piper/`.
3. Elegí salida **Todo** y dejá **Permitir spilling** marcado.
4. Elegí las opciones de la prueba y presioná **F5**.
5. En **Compilación**, abrí las IR, el reporte de registros y el ASM. **Abrir carpeta
   de resultados** permite localizar el `.mem` y el ZIP de esa ejecución.
6. Compilar no ejecuta automáticamente el programa. Podés pasar ese `.mem` a
   `sim.sh`, o usar **Medir optimizaciones** con PORYGON configurado en Métricas.

Para aislar lo que querés mostrar:

| Prueba | Selección sugerida | Qué observar |
| --- | --- | --- |
| Unrolling | Comparar U apagado / automático, con DCE y Rename apagados | El loop interior admite factor 2; el exterior se omite por contener un ciclo interno |
| DCE | Solo DCE activo | Desaparecen stores muertos entre `.unroll.ir` y `.opt.ir`; no todos los cálculos comprobados |
| Spilling | Primero permitido, luego prohibido | `.regalloc.txt` incluye slots/cargas/stores; sin permiso falla la asignación |
| Reordenamiento | DCE apagado; comparar Rename apagado / encendido | Dependencias, cambios de registro y bundles; scheduler activo en ambos |
| Clones | Pestaña **Clones**, mínimo 12 nodos, **Detectar clones** | Cuerpo idéntico de ventas/gastos y estructura renombrada de temperaturas |

El detector del IDE recorre archivos del proyecto, por lo que puede mostrar otros
grupos además de los de este ejemplo. Para aislarlo por terminal:

```bash
bin/pipoc --clones 12 --clones-exact tests/defensa/piper/IdentificacionDeClones.rwt
bin/pipoc --clones 12 --clones-renamed tests/defensa/piper/IdentificacionDeClones.rwt
```

## Resultados comprobados

En esta revisión, los cinco programas devolvieron el valor esperado en ambas
configuraciones, con `errors=0`:

| Programa | Ciclos base | Ciclos con todas las opciones |
| --- | --- | --- |
| IdentificacionDeClones | 9724 | 9710 |
| LoopUnrolling | 1269 | 1195 |
| Reordenamiento | 945 | 947 |
| SpillingDeRegistros | 1012 | 1012 |
| pruebaEliminacionCodigo | 2987 | 2965 |

La comparación cambia varias opciones juntas; no atribuye la diferencia a una sola
pasada. En Reordenamiento hay una pequeña regresión, no una mejora garantizada.
El caso de spilling usó cuatro slots en `main`. El de matrices eliminó cinco
instrucciones IR con DCE. Los tiempos dependen de esta versión y estos datos.

## Limpiar y reconstruir

`make clean` ejecuta **`rm -rf build bin`** en este repositorio. Borra resultados,
objetos, programas compilados, carpetas de ejecuciones del IDE y cualquier fuente
que hayas guardado dentro de `build/`, incluido el antiguo `maraton.rwt` de la guía.

Las traducciones de `tests/defensa/piper/`, los originales Python y `ide/.venv/`
se conservan. Antes de limpiar, copiá fuera de `build/` lo que quieras guardar.
Después:

```bash
make clean
make all
python3 tests/defensa/comprobar_traducciones.py --sim
```

No hace falta reinstalar el IDE después de `make clean`. Tampoco limpia el
`build/` del repositorio vecino PORYGON.
