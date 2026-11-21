# Dos comprobaciones paso a paso del compilador

Ejecutadas el 21 de noviembre de 2026. Requieren GCC, Flex, Bison, Python 3 para
construir las herramientas y, para ejecutar PORYGON, `iverilog` y `vvp`.
Las comprobaciones se hacen con comandos de terminal, sin abrir el IDE.

**La IR inicial y la optimizada son dos puntos de la compilacion.** No se
reconstruye una IR a partir del binario. La segunda es la que se entrega a
regalloc y codeOPgen para generar el ejecutable optimizado.

## 1. Funcion con asignaciones, suma y multiplicacion

Fuente: [funcion_suma.rwt](../tests/codeOPgen/paso-a-paso/funcion_suma.rwt).

```text
routine B32 sumar_y_duplicar[B32 a, B32 b] {
    create B32 suma.
    create B32 doble.
    create B32 descartado.
    descartado store 99.
    suma store a + b.
    doble store suma * 2.
    output doble.
}

routine B32 main[] {
    create B32 x.
    create B32 y.
    create B32 resultado.
    x store 4.
    y store 3.
    resultado store sumar_y_duplicar[x,y].
    output resultado.
}
```

Resultado calculado a mano: `(4 + 3) * 2 = 14`.

Preparar la terminal desde la raiz de PIPER-COMPILER:

```bash
make all
nombre_prueba=funcion_suma
factor_unroll=off
resultado_esperado=14
```

Ejecutar los pasos de la seccion 3. Cambiar solo DCE: unrolling permanece apagado.

IR esperada completa:

- [Inicial](../tests/codeOPgen/paso-a-paso/esperados/funcion_suma.inicial.ir).
- [Optimizada](../tests/codeOPgen/paso-a-paso/esperados/funcion_suma.optimizado.ir).

La IR inicial contiene:

```text
descartado = 99
t0 = a
t1 = b
t2 = t0 + t1 ; comprueba rango del resultado B32, puede fallar
suma = t2
t3 = suma
t4 = t3 * 2 ; comprueba rango del resultado B32, puede fallar
doble = t4
```

Despues de DCE desaparece `descartado = 99`. Las asignaciones utiles, llamada,
suma, multiplicacion y comprobaciones de rango se conservan: **18 → 17 instrucciones IR**.
La declaracion de `descartado` puede seguir en la cabecera; eliminar esa
asignacion no reduce automaticamente el espacio de variables reservado en pila.

## 2. Loop que lee una matriz

Fuente: [loop_matriz.rwt](../tests/codeOPgen/paso-a-paso/loop_matriz.rwt).

```text
routine B32 main[] {
    create B32|2&2| matriz.
    create B32 fila.
    create B32 suma.
    create B32 descartado.
    matriz store [[1,2],[3,4]].
    descartado store 99.
    suma store 0.
    loop [fila & 0 -> 2 & 1] {
        suma store suma + matriz|fila&1|.
    }
    output suma.
}
```

Lee la columna de indice 1: `matriz[0][1] + matriz[1][1] = 2 + 4 = 6`.

En la misma terminal:

```bash
nombre_prueba=loop_matriz
factor_unroll=2
resultado_esperado=6
```

Repetir los pasos de la seccion 3. Aqui se activan unrolling factor 2 y DCE.

IR esperada completa:

- [Inicial](../tests/codeOPgen/paso-a-paso/esperados/loop_matriz.inicial.ir).
- [Optimizada](../tests/codeOPgen/paso-a-paso/esperados/loop_matriz.optimizado.ir).

Comprobar en esos archivos:

- Inicial: cinco bloques, un cuerpo de loop y retorno del bloque de incremento a B1.
- Optimizada: siete bloques; B4 ahora salta a B5, que contiene la segunda copia del cuerpo. B6 incrementa la fila y vuelve a B1.
- Desaparece `descartado = 99`. Se conservan escrituras de la matriz y comprobaciones de indices.
- Direccion del elemento: `base + (fila * 2 + 1) * 4`. Para filas 0/1 son desplazamientos 4/12, donde estan 2/4.
- Conteos: **34 instrucciones iniciales → 51 tras unrolling → 50 tras DCE**.

## 3. Comandos para cada prueba

### A. Revisar fuente y producir IR inicial

```bash
fuente_prueba="tests/codeOPgen/paso-a-paso/$nombre_prueba.rwt"
salida_prueba="build/runs/codeOPgen/paso-a-paso/$nombre_prueba"
mkdir -p "$salida_prueba"
bin/pipoc --check "$fuente_prueba"
bin/pipoc ir "$fuente_prueba" -o "$salida_prueba/inicial.ir" \
  --emit-cfg "$salida_prueba/inicial.dot"
cat "$salida_prueba/inicial.ir"
```

Esperado: revision sin errores; `--check` termina con codigo 0 y no imprime errores.

### B. Compilar sin unrolling ni DCE

```bash
bin/pipoc "$fuente_prueba" -o "$salida_prueba/sin-opt.bin" \
  --emit-ir "$salida_prueba/compilacion-sin-opt.ir" \
  --emit-asm "$salida_prueba/sin-opt.asm" \
  --emit-mem "$salida_prueba/sin-opt.mem" \
  --emit-bundles "$salida_prueba/sin-opt.bundles" -v
diff -u "$salida_prueba/inicial.ir" "$salida_prueba/compilacion-sin-opt.ir"
```

Esperado: genera ASM/binario/memoria/bundles; `diff` no imprime diferencias.
Asignacion de registros y scheduler siguen activos en este modo.

### C. Compilar con las optimizaciones elegidas

```bash
bin/pipoc "$fuente_prueba" -o "$salida_prueba/optimizado.bin" \
  --unroll "$factor_unroll" --dce --spill on \
  --emit-ir "$salida_prueba/antes-dce.ir" \
  --emit-ir-opt "$salida_prueba/optimizado.ir" \
  --emit-cfg-opt "$salida_prueba/optimizado.dot" \
  --emit-opt-report "$salida_prueba/optimizacion.json" \
  --emit-asm "$salida_prueba/optimizado.asm" \
  --emit-mem "$salida_prueba/optimizado.mem" \
  --emit-bundles "$salida_prueba/optimizado.bundles" -v
cat "$salida_prueba/optimizado.ir"
cat "$salida_prueba/optimizacion.json"
diff -u "tests/codeOPgen/paso-a-paso/esperados/$nombre_prueba.inicial.ir" \
  "$salida_prueba/inicial.ir"
diff -u "tests/codeOPgen/paso-a-paso/esperados/$nombre_prueba.optimizado.ir" \
  "$salida_prueba/optimizado.ir"
```

Esperado: ambos `diff` sin diferencias. En ambos casos, DCE informa una instruccion
eliminada. `antes-dce.ir` ya incluye unrolling si se selecciono; `inicial.ir` no.
El reporte de regalloc indica **cero spills** en estas dos pruebas.

### D. Verificar que binario y memoria contienen las mismas instrucciones

```bash
bin/pipoc --bin-to-mem "$salida_prueba/optimizado.bin" \
  -o "$salida_prueba/convertido.mem"
diff -u "$salida_prueba/optimizado.mem" "$salida_prueba/convertido.mem"
```

Esperado: sin diferencias. El ensamblado y la verificacion del scheduler ya se
ejecutaron al producir esos archivos.

### E. Ejecutar ambas versiones y comprobar resultado y pila

```bash
../PORYGON/tools/sim.sh "$salida_prueba/sin-opt.mem" \
  --out "$salida_prueba/sin-opt.sim" --max-cycles 10000 \
  --expect "r1=$resultado_esperado" --expect r7=131072 \
  --expect errors=0 --expect halted=1 --expect timeout=0
../PORYGON/tools/sim.sh "$salida_prueba/optimizado.mem" \
  --out "$salida_prueba/optimizado.sim" --max-cycles 10000 \
  --expect "r1=$resultado_esperado" --expect r7=131072 \
  --expect errors=0 --expect halted=1 --expect timeout=0
```

Esperado: ambos comandos terminan en `PASS`. r1 contiene 14 o 6; r7 vuelve a
`0x00020000`, la direccion inicial de pila. Los resultados estan en los archivos
`sin-opt.sim/results.txt` y `optimizado.sim/results.txt`.

## 4. Resultados medidos

| Prueba | Version | Retorno | Instrucciones IR | Bytes del .bin | Ciclos |
| --- | --- | --- | --- | --- | --- |
| Funcion | Sin unroll/DCE | 14 | 18 | 1664 | 189 |
| Funcion | DCE | 14 | 17 | 1608 | 182 |
| Matriz | Sin unroll/DCE | 6 | 34 | 1720 | 310 |
| Matriz | Unroll 2 + DCE | 6 | 50 | 2320 | 286 |

Son mediciones de esta version, no limites fijos para versiones futuras. En la
matriz crece el codigo porque se copia el cuerpo, pero se ejecutan menos ciclos.
En las cuatro ejecuciones: cero errores, sin timeout y pila restaurada.
