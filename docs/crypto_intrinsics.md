# Intrínsecas Feistel4 y ejecución con el loader

Las cuatro intrínsecas están disponibles sin declarar rutinas ni importar archivos.
Sus nombres están reservados. Se usan como sentencias `nombre[argumentos].` y
devuelven `none` (`TYPE_VOID`): no se pueden asignar ni usar como valores.

## Firmas y comportamiento

| Llamada | Argumentos y efecto |
| --- | --- |
| `crypto_auth[w0, w1, w2, w3].` | Cuatro expresiones enteras de hasta 32 bits; carga la credencial y autentica la bóveda. `w0` es la palabra menos significativa de los 128 bits. |
| `crypto_write_key[subkey, sk_idx, k_idx].` | Una expresión entera de hasta 32 bits y dos índices; escribe una subllave de la llave seleccionada. Requiere autenticación. |
| `crypto_feistel[block, rnd_idx, k_idx].` | `block` debe ser el nombre de un arreglo **B32\|2\|**, global, local o parámetro. Ejecuta una ronda y modifica el mismo arreglo: `block\|0\|` contiene L y `block\|1\|` contiene R. Requiere autenticación. |
| `crypto_revoke[].` | Sin argumentos. Revoca la sesión y limpia la credencial; no borra las subllaves. |

Los índices deben ser **literales enteros de 0 a 3**, porque el hardware los
codifica como inmediatos. No se admiten variables ni expresiones en esas posiciones.
Las palabras de autenticación y las subllaves sí admiten variables, operaciones y
llamadas con retorno entero. Los booleanos y los arreglos no son palabras enteras.
Se conservan las validaciones existentes de rango y de variables sin inicializar.

Cada ronda calcula, con aritmética de 32 bits:

```text
F(R, k) = ((ROL32(R, 5) + k) mod 2^32) XOR ROL32(R, 13)
L' = R
R' = L XOR F(R, k)
```

Una llamada hace una sola ronda; Feistel4 requiere las rondas 0, 1, 2 y 3.
La subllave utilizada es `keys[k_idx][rnd_idx]`.

## Ensamblador generado

Los índices CRYPTO del ensamblador de este repositorio se escriben **sin `#`**.
Las palabras se evalúan en `r1` con el generador de expresiones existente; los
literales grandes se construyen con `mov`, `sumi` y `shli`, no con `lowi #valor`.
Por ejemplo, `3757111146` (`0xDFF0F76A`) genera:

```asm
mov r1, r0
sumi r1, #-8207
shli r1, #16
sumi r1, #-2198
```

`crypto_auth` evalúa cada palabra en orden, emitiendo después de cada evaluación
`lopw r1, 0`, `lopw r1, 1`, `lopw r1, 2` y `lopw r1, 3`; termina con `auth`.
`crypto_write_key` evalúa `subkey` en `r1` y emite `write r1, sk_idx, k_idx`.
`crypto_revoke` emite `rvk`.

`crypto_feistel` obtiene la dirección mediante `load_array_base(ctx, block, r3)`
y emite esta secuencia (sustituyendo los dos índices por sus literales):

```asm
lowi r1, 0(r3)
lowi r2, 4(r3)
feistel r1, r2, rnd_idx, k_idx
stwi r1, 0(r3)
stwi r2, 4(r3)
```

El scheduler existente introduce las esperas necesarias para leer ambos resultados.

## Ejemplo reproducible

El ejemplo `crypto_feistel_pipeline.rwt` se conserva en la carpeta externa de
pruebas. Definir `PIPER_TESTS_DIR` con la ruta a esa carpeta para los comandos
de esta sección. Su contenido es:

```piper
create B32|2| block.

routine B32 main[] {
    when [block|0| = 0 and block|1| = 0] {
        block store [19088743, -1985229329].
    }
    crypto_auth[3757111146, 2846257176, 3662207751, 342038459].
    crypto_write_key[2712847316, 0, 0].
    crypto_feistel[block, 0, 0].
    crypto_feistel[block, 1, 0].
    crypto_feistel[block, 2, 0].
    crypto_feistel[block, 3, 0].
    crypto_revoke[].
    output block|0|.
}
```

Piper acepta literales decimales. El valor negativo representa los bits
`0x89ABCDEF` dentro de un `B32`. La contraseña inicial real del RTL es
`0x146317BBDA48DB07A9A67018DFF0F76A`; las cuatro palabras se pasan de menor a
mayor significancia. La subllave 0 es `0xA1B2C3D4`; las otras tres quedan en
cero por el reset de cada simulación.

La bóveda caduca después de **32 ciclos inactivos**. Cada `write` o ronda Feistel
autorizada reinicia el contador. Este ejemplo deja 17 ciclos entre rondas, por
lo que basta una autenticación inicial: genera **325 bundles (2600 bytes)**.
Requiere el RTL con `AUTH_TIMEOUT_CYCLES = 32` en `key_vault.sv`.
Conviene calcular subllaves complejas
antes de autenticar y pasarlas mediante una variable: evaluar una expresión
larga dentro de `crypto_write_key` también puede agotar la sesión. Las
intrínsecas no ejecutan otra autenticación automáticamente ni devuelven su estado;
si la sesión ya caducó, se debe volver a llamar a `crypto_auth`.

Desde `PIPER-COMPILER`, con `make`, GCC, Flex, Bison, Icarus Verilog (`iverilog`,
`vvp`) y Python 3 disponibles:

```sh
make clean && make all
./run.sh "$PIPER_TESTS_DIR/codegen/crypto_feistel_pipeline.rwt" 500 \
  --expect errors=0 --expect r1=0x751eef3b \
  --expect r7=0x20000 --expect authenticated=0
```

`run.sh` compila el `.rwt` a memoria de instrucciones y llama a `PORYGON/sim.sh`.
Busca PORYGON en `../../ProyectoArqui/PORYGON`; para otra ubicación, definir
`PORYGON_DIR=/ruta/a/PORYGON`. Los resultados quedan en
`build/runs/crypto_feistel_pipeline/`: `program.mem`, `results.txt`,
`simulation.log`, `memory.dump.mem` y `waves.vcd`.

Resultado verificado en RTL a 500 ciclos: `errors=0`, `r1=0x751eef3b`,
`r7=0x00020000` y `authenticated=0`. También se extrajeron los ocho bytes
de RAM: las dos mitades son **`751EEF3B 47650321`**, coincidentes con un
cálculo independiente de las cuatro rondas.

## Cargar un bloque desde un archivo y recuperar el cifrado

El loader convierte **datos binarios** a `.mem`; el `.rwt` lo compila `pipoc`
a través de `run.sh`. El primer global del ejemplo ocupa ocho bytes desde
`0x10000`: L en los primeros cuatro y R en los siguientes cuatro, en little endian.
El ejemplo usa un bloque predeterminado únicamente si ambas palabras son cero.
Para cifrar un bloque de ocho ceros, eliminar ese `when` de inicialización.

Estos comandos, desde `PIPER-COMPILER`, prueban un bloque diferente al predeterminado:

```sh
export PORYGON_DIR="$(cd ../../ProyectoArqui/PORYGON && pwd)"
mkdir -p build/crypto-loader
python3 - <<'PY'
from pathlib import Path
from struct import pack
Path('build/crypto-loader/input.bin').write_bytes(pack('<II', 0x11223344, 0x55667788))
PY
python3 "$PORYGON_DIR/tools/data_loader/load_file.py" \
  --input build/crypto-loader/input.bin --output build/crypto-loader/data.mem \
  --address 0x10000 --width 8 --mem-base 0x10000
./run.sh "$PIPER_TESTS_DIR/codegen/crypto_feistel_pipeline.rwt" 500 \
  --data build/crypto-loader/data.mem --out build/crypto-loader/run \
  --expect errors=0 --expect r1=0x631ce6e1 --expect authenticated=0
"$PORYGON_DIR/extract.sh" build/crypto-loader/run/memory.dump.mem \
  0x10000 8 encrypted.bin
python3 - <<'PY'
from pathlib import Path
from struct import unpack
data = Path('build/crypto-loader/run/encrypted.bin').read_bytes()
print(' '.join(f'{word:08X}' for word in unpack('<II', data)))
PY
```

Salida verificada: **`631CE6E1 D1D1F33F`**; bytes del archivo:
`e1 e6 1c 63 3f f3 d1 d1`. El extractor coloca `encrypted.bin` junto al dump.
Para usar un archivo propio, sustituir `input.bin` y ajustar la expectativa de
`r1`; este programa procesa exactamente un bloque de ocho bytes.

## Validación realizada

Esta sección registra la validación histórica; la suite de pruebas se conserva
fuera del repositorio.

Pasaron la compilación limpia, `test-cli-bin` y `test-scheduler` (incluidos sus
100 programas diferenciales). Las comprobaciones temporales cubrieron 27
rechazos semánticos, arreglos globales/locales/por parámetro, palabras calculadas,
índices 3, contraseña incorrecta y acceso después de revocar. En estos dos
últimos casos se comprobaron los errores de hardware esperados. También pasó
el recorrido loader → RTL → extractor con comparación de ambas mitades.
Los casos y archivos temporales se eliminaron; se conserva el `.rwt` de ejemplo
y las pruebas preexistentes del proyecto.
