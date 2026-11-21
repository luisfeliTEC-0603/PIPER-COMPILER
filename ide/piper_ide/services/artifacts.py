"""Archivos de una ejecución y métricas reales de pipoc -v.

El JSON del IDE reúne opciones y mensajes de los módulos. No estima ciclos
ni confunde cargas/stores estáticos con accesos durante la ejecución.
Si cambia un mensaje, se conserva el detalle sin inventar conteos.
"""

from dataclasses import asdict
import json
from pathlib import Path
import re

from ..models.compilation import CompilationResult


def compilation_report(result: CompilationResult) -> dict:
    invocation = result.invocation
    options = invocation.optimizations
    inspection = invocation.output_format == "ins"
    lines = result.stderr.splitlines()
    unroll_lines = [line for line in lines if line.startswith("[unroll]")]
    dce_lines = [line for line in lines if line.startswith("[pipoc] DCE:")]
    allocation_lines = [line for line in lines if line.startswith("[regalloc]")
                        or line.startswith("  t") or line.startswith("  B")]
    loops = re.search(r"loops: analizados=(\d+) desenrollados=(\d+) rechazados=(\d+)", result.stderr)
    dce = re.search(r"DCE: (\d+) -> (\d+) instrucciones IR \((\d+) eliminadas, (\d+) rutinas eliminadas\)", result.stderr)
    allocations = [dict(zip(("routine", "spilled_temporaries", "slots", "stack_bytes", "loads", "stores"),
                            map(int, match))) for match in re.findall(
        r"\[regalloc\] rutina (\d+): temporales=(\d+) slots=(\d+) frame=(\d+) cargas=(\d+) stores=(\d+)", result.stderr)]

    def pass_result(enabled, completed, changed, details):
        if inspection:
            return {"status": "omitida", "reason": "AST y símbolos: solo análisis del código", "details": []}
        if not enabled:
            return {"status": "omitida", "reason": "Desactivada por el usuario", "details": []}
        if not completed:
            return {"status": "sin resultado", "reason": "La compilación no completó esta pasada", "details": details}
        return {"status": "aplicada" if changed else "sin cambios",
                "reason": "Consultar decisiones del módulo" if details else "", "details": details}

    unrolling = pass_result(options.unroll, loops is not None,
                           bool(loops and int(loops[2])), unroll_lines)
    if loops:
        unrolling["loops"] = dict(zip(("analyzed", "unrolled", "skipped"), map(int, loops.groups())))
    unroll_counts = re.search(r"\[unroll\] instrucciones: antes=(\d+) después=(\d+)", result.stderr)
    if unroll_counts:
        unrolling["instructions"] = dict(zip(("before", "after"), map(int, unroll_counts.groups())))
    elimination = pass_result(options.dce, dce is not None,
                              bool(dce and (int(dce[3]) or int(dce[4]))), dce_lines)
    if dce:
        elimination["instructions"] = dict(zip(("before", "after", "removed", "removed_routines"), map(int, dce.groups())))
    spilling = pass_result(True, bool(allocations),
                          any(item["slots"] for item in allocations), allocation_lines)
    spilling["allowed"] = options.allow_spill
    spilling["routines"] = allocations
    if allocations and not any(item["slots"] for item in allocations):
        spilling["reason"] = "La asignación cabe en registros; no requiere spilling"
    if 'Habilite "Permitir spilling"' in result.stderr:
        spilling["status"] = "requiere spilling"
        spilling["reason"] = 'Habilitá «Permitir spilling» y compilá nuevamente.'
    return {
        "producer": "Piper IDE (mensajes reales de pipoc -v)",
        "source": str(invocation.source), "command": invocation.display_command,
        "succeeded": result.succeeded, "exit_code": result.exit_code,
        "options": asdict(options), "inspection_only": inspection,
        "passes": {"unrolling": unrolling, "DCE": elimination, "spilling": spilling},
        "scheduler": ("aplicado" if result.succeeded else "iniciado; compilación fallida")
                     if "[pipoc] Calendarización de " in result.stderr else "no ejecutado (solo análisis/ASM o fallo previo)",
        "artifact_error": result.artifact_error,
        "diagnostics": [line for line in lines if "error" in line.casefold() and not line.startswith("[pipoc]")],
    }


def report_text(result: CompilationResult) -> str:
    """Resumen visible, seguido de decisiones y ubicaciones de los módulos."""
    report = compilation_report(result)
    heading = "Compilación completada" if result.succeeded else "Compilación fallida"
    selection = ("Solo AST y símbolos; las optimizaciones no se ejecutan."
                 if report["inspection_only"] else result.invocation.optimizations.description)
    lines = [heading, selection, f"Carpeta: {result.invocation.output_dir}", ""]
    for name, item in report["passes"].items():
        lines.extend((f"{name}: {item['status']}. {item['reason']}", *item["details"], ""))
    lines.extend(report["diagnostics"])
    if result.artifact_error:
        lines.append(result.artifact_error)
    if not result.succeeded:
        # Los diagnósticos del parser pueden incluir líneas sin prefijo ni
        # palabra «error». Preservar también ese contexto, igual que Consola.
        lines.extend(("", "Salida completa del intento fallido:", result.stderr, result.stdout))
    lines.append(f"Scheduler: {report['scheduler']}")
    return "\n".join(lines)


def publish_artifacts(result: CompilationResult) -> None:
    """Solo acepta archivos de la carpeta exclusiva de esta ejecución exitosa."""
    if not result.succeeded:
        return
    reports = [path for path in result.invocation.artifacts if path.name.endswith(".opt.json")]
    for path in result.invocation.artifacts:
        if path not in reports and not path.is_file():
            raise OSError(f"Pipoc no generó el archivo esperado: {path.name}")
    for path in reports:
        path.write_text(json.dumps(compilation_report(result), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def visible_artifacts(result: CompilationResult) -> tuple[Path, ...]:
    """Un error nunca publica como vigentes salidas parciales o anteriores."""
    return result.invocation.artifacts if result.succeeded else ()
