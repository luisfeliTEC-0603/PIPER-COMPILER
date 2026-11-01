const vscode = require('vscode');
const path = require('node:path');
const fs = require('node:fs/promises');
const { execFile } = require('node:child_process');
const { formats, buildInvocation, parseDiagnostics } = require('./compiler');

function activate(context) {
  const output = vscode.window.createOutputChannel('Piper');
  const diagnostics = vscode.languages.createDiagnosticCollection('piper');
  const running = new Set();
  const children = new Set();
  context.subscriptions.push(output, diagnostics, { dispose: () => { for (const child of children) child.kill(); } });

  async function compile(document, selectedFormat, automatic = false) {
    if (!document || document.languageId !== 'piper') return;
    if (!vscode.workspace.isTrusted) {
      if (!automatic) vscode.window.showWarningMessage('Piper: confía en el espacio de trabajo para ejecutar pipoc.');
      return;
    }
    if (document.isUntitled || document.uri.scheme !== 'file') {
      if (!automatic) vscode.window.showWarningMessage('Piper: guarda el archivo .rwt en disco antes de compilar.');
      return;
    }
    const key = document.uri.toString();
    if (running.has(key)) return;
    running.add(key);
    try {
      if (document.isDirty && !(await document.save())) return;
      const version = document.version;
      const source = document.uri.fsPath;
      const root = vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath || path.dirname(source);
      const config = vscode.workspace.getConfiguration('piper', document.uri);
      let compiler = config.get('compilerPath', '').trim();
      if (compiler) {
        compiler = path.isAbsolute(compiler) ? compiler : path.resolve(root, compiler);
      } else {
        const local = path.join(root, 'bin', process.platform === 'win32' ? 'pipoc.exe' : 'pipoc');
        compiler = await fs.access(local).then(() => local, () => 'pipoc');
      }
      const format = selectedFormat || config.get('outputFormat', 'bin');
      const invocation = buildInvocation(source, root, format);
      await fs.mkdir(invocation.directory, { recursive: true });
      output.appendLine(`\nPiper: ${source} (${format})`);
      diagnostics.delete(document.uri);
      const result = await new Promise(resolve => {
        const child = execFile(compiler, invocation.args, { cwd: root, timeout: 120000, maxBuffer: 8 * 1024 * 1024 }, (error, stdout, stderr) => {
          children.delete(child);
          resolve({ error, stdout, stderr });
        });
        children.add(child);
      });
      if (result.stdout) output.append(result.stdout);
      if (result.stderr) output.append(result.stderr);
      // No publicar ubicaciones correspondientes a una versión anterior del texto.
      if (!document.isClosed && document.version === version) {
        const problems = parseDiagnostics(result.stderr).map(problem => {
          const line = Math.min(problem.line, document.lineCount - 1);
          const column = Math.min(problem.column, document.lineAt(line).text.length);
          const range = new vscode.Range(line, column, line, Math.min(column + 1, document.lineAt(line).text.length));
          const diagnostic = new vscode.Diagnostic(range, problem.message, vscode.DiagnosticSeverity.Error);
          diagnostic.source = 'pipoc';
          return diagnostic;
        });
        diagnostics.set(document.uri, problems);
      }
      if (result.error) {
        const message = result.error.code === 'ENOENT'
          ? 'No se encontró pipoc. Ejecuta make all en el compilador o configura piper.compilerPath.'
          : result.error.code === 'EACCES'
            ? 'No se pudo ejecutar pipoc. Revisa los permisos de piper.compilerPath.'
            : result.error.killed
              ? 'La compilación se detuvo o superó el límite de 120 segundos.'
              : 'La compilación falló. Revisa Problemas y el canal de salida Piper.';
        output.appendLine(message);
        if (!automatic) {
          output.show(true);
          vscode.window.showErrorMessage(`Piper: ${message}`);
        }
      } else {
        output.appendLine(`Artefactos: ${invocation.directory}`);
        if (!automatic) vscode.window.showInformationMessage(`Piper: compilación ${format} completada. Salidas en build/vscode/.`);
      }
    } catch (error) {
      output.appendLine(`Piper: ${error.message}`);
      if (!automatic) vscode.window.showErrorMessage(`Piper: ${error.message}`);
    } finally {
      running.delete(key);
    }
  }

  context.subscriptions.push(
    vscode.commands.registerCommand('piper.compile', () => compile(vscode.window.activeTextEditor?.document)),
    vscode.commands.registerCommand('piper.compileAs', async () => {
      const document = vscode.window.activeTextEditor?.document;
      const format = await vscode.window.showQuickPick(formats, { placeHolder: 'Salida de pipoc: bin, asm, sch (bundles), mem, ins (AST/símbolos), all' });
      if (format) await compile(document, format);
    }),
    vscode.workspace.onDidSaveTextDocument(document => {
      if (vscode.workspace.getConfiguration('piper', document.uri).get('compileOnSave', false)) void compile(document, undefined, true);
    }),
    vscode.workspace.onDidChangeTextDocument(event => diagnostics.delete(event.document.uri)),
    vscode.workspace.onDidCloseTextDocument(document => diagnostics.delete(document.uri))
  );
}

module.exports = { activate };
