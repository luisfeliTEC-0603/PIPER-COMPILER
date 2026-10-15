# Piper para VS Code

Extensión local para archivos `.rwt`: colores, comentarios (`~~` y `***`),
cierre de llaves y corchetes, indentación y snippets. Permite compilar con
`pipoc` y publicar errores léxicos, sintácticos y semánticos en **Problemas**.

## Instalar

Desde la raíz de PIPER-COMPILER:

```sh
make all
python3 editors/vscode/package.py
code --install-extension build/vscode-extension/piper-language-0.1.0.vsix
```

También puedes usar **Extensions: Install from VSIX…** desde la paleta de
comandos y elegir ese archivo. El empaquetado usa Python 3 sin dependencias.
Los colores y snippets funcionan sin tener el compilador instalado.

