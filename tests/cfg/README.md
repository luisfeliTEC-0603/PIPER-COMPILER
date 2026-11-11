# Revisión manual del CFG

Desde la raíz del repositorio:

```sh
make all
mkdir -p build/runs/cfg
bin/pipoc ir tests/ir/scalar.rwt -o build/runs/cfg/scalar.ir --emit-cfg build/runs/cfg/scalar.dot
bin/pipoc ir tests/ir/branches.rwt -o build/runs/cfg/branches.ir --emit-cfg build/runs/cfg/branches.dot
bin/pipoc ir tests/ir/loop_break.rwt -o build/runs/cfg/loop_break.ir --emit-cfg build/runs/cfg/loop_break.dot
bin/pipoc ir tests/cfg/calls.rwt -o build/runs/cfg/calls.ir --emit-cfg build/runs/cfg/calls.dot
```

| Caso | Revisar |
| --- | --- |
| scalar | ENTRY → B0 → EXIT. |
| branches | TRUE/FALSE desde B0 y convergencia en B3; conservar ambas ramas. |
| loop_break | Vuelta de B5 a B1 y salida de break de B4 a B3. |
| calls | Tres subgrafos: incrementar, main y arranque. Las llamadas aparecen en TAC, sin aristas entre rutinas. |

Para inspeccionar también matrices y Feistel:

```sh
bin/pipoc ir tests/ir/matrix.rwt -o build/runs/cfg/matrix.ir --emit-cfg build/runs/cfg/matrix.dot
bin/pipoc ir tests/ir/feistel.rwt -o build/runs/cfg/feistel.ir --emit-cfg build/runs/cfg/feistel.dot
```

Sus lecturas/escrituras, límites y efectos están en la etiqueta B0; no añaden conexiones nuevas.

Los seis DOT de referencia están en `tests/cfg/expected/`. Comparar, por ejemplo:

```sh
diff -u tests/cfg/expected/branches.dot build/runs/cfg/branches.dot
```

Sin diferencias, diff no imprime nada y termina con estado 0. Los archivos generados permanecen en build/runs/cfg.

## Dibujar el DOT

El DOT es texto legible. Para convertirlo a SVG si Graphviz está instalado:

```sh
dot -Tsvg build/runs/cfg/branches.dot -o build/runs/cfg/branches.svg
```

Abrir el SVG en un navegador o visor. Graphviz solo es necesario para dibujar, no para construir pipoc ni exportar DOT.

## Compilar además el binario

```sh
bin/pipoc tests/ir/branches.rwt -o build/runs/cfg/branches.bin --emit-cfg build/runs/cfg/branches.dot
```

Este modo genera binario y DOT; no escribe `.ir` salvo que se añada `--emit-ir`. El backend actual conserva su entrada AST/símbolos. `all` añade CFG solo al pedir `--emit-cfg`. Las opciones de optimización siguen pendientes.

Los casos internos de nodos inalcanzables, ciclos cerrados y ramas con ambos destinos iguales se comprobaron durante desarrollo con IR preparada. No tienen una fuente Piper equivalente garantizada porque el generador conserva/omite esos caminos según sus reglas; no se añade runner de pruebas al repositorio.
