# Pruebas finales de IR

Desde la raíz del repositorio, construir con `make all` y generar los archivos con los comandos de abajo. Las cinco fuentes quedan disponibles para revisión manual, sin runner Python.

| Caso | Fuente | Resultado esperado | Qué comprueba |
| --- | --- | --- | --- |
| IR-T1 | `scalar.rwt` | 14 | Lecturas, precedencia, tipos, flags y un bloque. |
| IR-T2 | `branches.rwt` | 1; 2 al cambiar p a false | Dos ramas y unión, condición bool y ninguna eliminación de la rama constante. |
| IR-T3 | `loop_break.rwt` | 2 | Reevaluación de condición, incremento, salida de break y bloques cerrados correctamente. |
| IR-T4 | `matrix.rwt` | 10 | Matriz B16 de 2×3: inicialización, límites de ambos índices, ancho de dos bytes, orden por filas, escritura y lecturas. |
| IR-T5 | `feistel.rwt` | Traducción IR | Preparación criptográfica, dirección del bloque B32 de dos elementos, índices de clave, efectos y lectura posterior a Feistel. |

Las salidas esperadas quedan en `tests/ir/expected/` como referencia. Revisar los `.ir` generados: tipos de cabecera, operaciones, límites, destinos y efectos. Los resultados numéricos de la tabla describen la ejecución esperada; generar IR no ejecuta el programa. Feistel permite revisar traducción y efectos, sin comprobar aquí el cifrado.

## Generar archivos para inspección manual

```sh
make all
mkdir -p build/runs/ir
bin/pipoc ir tests/ir/scalar.rwt -o build/runs/ir/scalar.ir
bin/pipoc ir tests/ir/branches.rwt -o build/runs/ir/branches.ir
bin/pipoc ir tests/ir/loop_break.rwt -o build/runs/ir/loop_break.ir
bin/pipoc ir tests/ir/matrix.rwt -o build/runs/ir/matrix.ir
bin/pipoc ir tests/ir/feistel.rwt -o build/runs/ir/feistel.ir
```

Para generar también el binario, con el backend actual:

```sh
bin/pipoc tests/ir/scalar.rwt -o build/runs/ir/scalar.bin --emit-ir build/runs/ir/scalar.ir
```

Sin `-o`, `bin/pipoc ir programa.rwt` guarda `programa.ir` junto al fuente. `all` añade IR únicamente si se solicita `--emit-ir`. Las opciones de CFG y optimización siguen pendientes.

Para comparar un archivo con la referencia desde terminal:

```sh
diff -u tests/ir/expected/matrix.ir build/runs/ir/matrix.ir
```

Sin diferencias, diff no imprime nada y termina con estado 0. Los archivos generados permanecen en build/runs/ir para inspeccionarlos.
