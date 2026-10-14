# PIPER COMPILER 

<div align="center">
  <img src="docs/imgs/rowlet.png" alt="rowlet" width="20%" height="auto">

  [![Compilador](https://img.shields.io/badge/Compilador-C-ecc6a9?style=for-the-badge&logoColor=white)]()
  [![Arquitectura](https://img.shields.io/badge/Arquitectura-PORYGON-1eae89?style=for-the-badge&logoColor=white)]()
  [![Lenguaje](https://img.shields.io/badge/Lenguaje-Piper-ecc6a9?style=for-the-badge&logoColor=white)]()

  **Compilador en C que traduce el lenguaje `Piper` a binario —específico para la arquitectura `PORYGON`**
</div>

## Salida del compilador

Todo programa debe declarar una rutina global `main[]`, sin parámetros.
Por ejemplo: `routine B32 main[] { output 0. }`.
El analizador semántico rechaza archivos sin esa entrada, incluso si contienen
sentencias globales. Los fixtures que solo declaran rutinas necesitan agregar
`main[]` para pasar el análisis semántico.

`bin/pipoc archivo.rwt` valida el programa sin imprimir el recorrido de las
fases. Los errores siempre aparecen en la consola. Las opciones de salida son:

| Opción | Contenido |
| --- | --- |
| `-l`, `--lexical` | Lexemas y posiciones entregados por el lexer |
| `-s`, `--syntactic` | Resultado del parser |
| `-m`, `--symbols` | Tabla de símbolos definitiva y memoria |
| `-e`, `--semantic` | Recorrido de comprobaciones semánticas |
| `-a`, `--all` | Las cuatro salidas anteriores |
| `-t`, `--ast-json` | Exporta el AST a `ast_output.json` |
| `-o ruta.json`, `--output ruta.json` | Cambia la ruta del JSON de `-t` |

Se pueden combinar opciones. El AST no se imprime en la consola; para verlo se
usa `-t`. Por ejemplo: `bin/pipoc -a -t programa.rwt`.
