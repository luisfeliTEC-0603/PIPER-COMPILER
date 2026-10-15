const path = require('node:path');

const formats = ['bin', 'asm', 'sch', 'mem', 'ins', 'all'];

function buildInvocation(source, root, format) {
  if (!formats.includes(format)) throw new Error(`Formato desconocido: ${format}`);
  const relative = path.relative(root, source);
  const inside = !path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`);
  const name = (inside ? relative : path.basename(source)).replace(/\.rwt$/i, '');
  const base = path.join(root, 'build', 'vscode', name);
  const args = format === 'bin' ? [] : [format];
  const extensions = { bin: '.bin', asm: '.asm', sch: '.bundles', mem: '.mem', all: '.bin' };
  if (extensions[format]) args.push('-o', base + extensions[format]);
  if (format === 'ins' || format === 'all') {
    args.push('--ast-json', base + '.ast.json', '--emit-symbols', base + '.symbols.txt');
  }
  if (format === 'all') {
    args.push('--emit-asm', base + '.asm', '--emit-bundles', base + '.bundles', '--emit-mem', base + '.mem');
  }
  args.push(source);
  return { args, directory: path.dirname(base) };
}

function parseDiagnostics(stderr) {
  const diagnostics = [];
  for (const line of stderr.split(/\r?\n/)) {
    const located = /^(\d+):(\d+):\s*(.+)$/.exec(line);
    const semantic = /^\[Error Semántico\] Línea (\d+):\s*(.+)$/.exec(line);
    if (located) {
      diagnostics.push({ line: Math.max(0, Number(located[1]) - 1), column: Math.max(0, Number(located[2]) - 1), message: located[3] });
    } else if (semantic) {
      diagnostics.push({ line: Math.max(0, Number(semantic[1]) - 1), column: 0, message: semantic[2] });
    }
  }
  return diagnostics;
}

module.exports = { formats, buildInvocation, parseDiagnostics };
