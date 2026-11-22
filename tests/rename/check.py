"""Compara dos compilaciones Piper y, con --sim, sus resultados en el RTL."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import subprocess
from tempfile import NamedTemporaryFile


OUT = Path("build/runs/rename")
OPERAND = re.compile(r"(t\d+)\((r\d+)\)")


def run(args, log):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"{log} ({result.returncode})\n{result.stdout}")
    return result.stdout


def operations(report):
    rows = {}
    for line in report.splitlines():
        if match := re.match(r"rutina \d+: ([^,]+),", line):
            routine = match[1]
        elif match := re.match(r"(B\d+):$", line):
            block = match[1]
        elif match := re.match(r"  (\d+): ", line):
            rows[routine, block, match[1]] = line
    return rows


def spill_lines(report):
    return [line for line in report.split("Maquina asignada")[0].splitlines()
            if line and not line.startswith("[rename]")]


def verify_report(base, variant):
    old, new = operations(base), operations(variant)
    assert old.keys() == new.keys()
    changes = set()
    affected = 0
    for location, before in old.items():
        after = new[location]
        assert OPERAND.sub(r"\1", before) == OPERAND.sub(r"\1", after), location
        affected += before != after
        for (temp, reg), (_, new_reg) in zip(OPERAND.findall(before), OPERAND.findall(after)):
            if reg != new_reg:
                changes.add((*location[:2], temp, reg, new_reg))
    declared = re.findall(r"^\[rename\] (\S+) (B\d+) (t\d+) (r\d+) -> (r\d+)$", variant, re.M)
    assert changes == set(declared) and len(changes) == len(declared) > 0
    assert f"[rename] temporales={len(changes)} operaciones_de_maquina_afectadas={affected}\n" in variant
    assert "[rename]" not in base
    # Incluye victima, slot, offset, frame y ubicacion de cada carga/store.
    assert spill_lines(base) == spill_lines(variant)
    return len(changes), affected


def issue_cycles(bundles, labels):
    first, end = labels["piper_funcion_0_bloque_0"], labels["piper_salida_funcion_0"]
    by_asm = {}
    for line in bundles.splitlines():
        if line.startswith("BUNDLE "):
            cycle = int(line.split()[1])
        elif match := re.match(r"\s+SLOT \d+ (?:MATH pipe=\d+|LOST) (.+)", line):
            if first <= cycle < end:
                by_asm.setdefault(match[1], []).append(cycle)
    return by_asm


def check_witness(name, report, asm, issues, enabled):
    def reg(pattern):
        return re.search(pattern, report)[1]

    if name == "raw":
        left, right = re.search(r"CHECKED_MUL def=t\d+\((r\d+)\) use1=t\d+\((r\d+)\)", report).groups()
        mul = f"mul {left}, {right}"
        store = f"stwi {left}, 0(r7)"
        for operand, value in ((left, 5), (right, 3)):
            producer = f"sumi {operand}, #{value}"
            assert asm.index(producer) < asm.index(mul) < asm.index(store)
            assert issues[producer][0] + 3 <= issues[mul][0]
        assert issues[mul][0] + 5 <= issues[store][0]
        return
    if name == "war":
        previous = reg(r"STORE_SYMBOL use1=t\d+\((r\d+)\) simbolo=primero")
        following = reg(r"IMM def=t\d+\((r\d+)\) inmediato=9 ")
        earlier = f"stwi {previous}, 0(r7)"
    else:
        previous = reg(r"LOAD_SYMBOL def=t\d+\((r\d+)\) simbolo=dato")
        following = reg(r"IMM def=t\d+\((r\d+)\) inmediato=7 ")
        earlier = f"lowi {previous}, 0(r7)"
    later = f"mov {following}, r0"
    assert later in asm.split(earlier, 1)[1]
    assert (previous != following) == enabled
    # En la referencia hay dos mov r1,r0. El segundo prepara el valor joven.
    assert (issues[later][-1] < issues[earlier][0]) == enabled


@contextmanager
def simulator_script(enabled):
    if not enabled:
        yield None
        return
    script = Path(os.environ.get("PORYGON_SIM", "../PORYGON/tools/sim.sh"))
    data = script.read_bytes()
    if b"\r\n" not in data:
        yield script
        return
    # Un checkout Windows puede usar CRLF. sim.sh localiza el RTL un nivel
    # encima de su directorio; una copia temporal en build/ conserva esa ruta.
    build = script.parent.parent / "build"
    build.mkdir(exist_ok=True)
    with NamedTemporaryFile(dir=build, suffix=".sh") as copy:
        copy.write(data.replace(b"\r\n", b"\n"))
        copy.flush()
        yield copy.name


def compile_case(name, mode, flags, expected, simulator):
    prefix = OUT / name / mode
    prefix.parent.mkdir(parents=True, exist_ok=True)
    log = run(["bin/pipoc", "all", f"tests/rename/{name}.rwt", "-v",
               "-o", prefix.with_suffix(".bin"), "--emit-regalloc", prefix.with_suffix(".regalloc.txt"),
               *flags, *(["--rename"] if mode == "rename" else [])], prefix.with_suffix(".compile.log"))
    report = prefix.with_suffix(".regalloc.txt").read_text()
    asm = prefix.with_suffix(".asm").read_text()
    bundles = prefix.with_suffix(".bundles").read_text()
    labels = {key: int(value) for key, value in re.findall(r"^LABEL (\S+) (\d+)$", bundles, re.M)}
    # Volver a calendarizar el ASM exportado debe reconstruir TODA la imagen.
    run(["bin/porygon-schedule", prefix.with_suffix(".asm"), prefix.with_suffix(".asm.bundles"),
         prefix.with_suffix(".asm.mem")], prefix.with_suffix(".schedule.log"))
    for extension in (".bundles", ".mem"):
        assert prefix.with_suffix(extension).read_bytes() == prefix.with_suffix(".asm" + extension).read_bytes()
    war, waw = map(int, run([OUT / "asm_hazards", prefix.with_suffix(".asm")],
                            prefix.with_suffix(".hazards.txt")).split())
    if name == "llamada":
        assert "aplicado; factor=2" in log
        assert json.loads(prefix.with_suffix(".dce.json").read_text())["removed_instructions"] > 0
        assert re.search(r"slots=[1-9]\d* .*cargas=[1-9]\d* stores=[1-9]", report)
        assert set(re.findall(r"CALL_ARG .* argumento=(\d+) ", report)) == set("012345")
    else:
        check_witness(name, report, asm, issue_cycles(bundles, labels), mode == "rename")
    cycles = None
    if simulator:
        run(["bash", simulator, prefix.with_suffix(".mem"), "--out", prefix, "--max-cycles", "50000",
             "--expect", f"r1={expected}", "--expect", "errors=0", "--expect", "halted=1",
             "--expect", f"halt_pc={8 * labels['piper_fin_programa']}"], prefix.with_suffix(".sim.log"))
        cycles = json.loads((prefix / "indicators.json").read_text())["contadores"]["cpu_cycles"]
        assert f"cycles={cycles}\n" in (prefix / "results.txt").read_text()
    return report, (war, waw, asm.count("\n    mov "), bundles.count("\nBUNDLE "), cycles)


def check_cases(simulator):
    rows = ["caso\tmodo\ttemporales\toperaciones\tWAR\tWAW\tmov\tbundles\tciclos"]
    for name, expected in (("war", 5), ("waw", 7), ("raw", 24), ("llamada", 123456)):
        flags = ["--unroll", "off", "--spill", "off"]
        if name == "llamada":
            flags = ["--dce", "--unroll", "2", "--spill", "on"]
        base, before = compile_case(name, "base", flags, expected, simulator)
        variant, after = compile_case(name, "rename", flags, expected, simulator)
        changes, affected = verify_report(base, variant)
        if name == "war":
            assert after[0] < before[0]
        if name == "waw":
            assert after[1] < before[1]
        assert after[2] <= before[2] and after[3] <= before[3], name
        if simulator:
            assert after[4] <= before[4], name
        for mode, counts in (("base", (0, 0, *before)), ("rename", (changes, affected, *after))):
            rows.append("\t".join(str(x) if x is not None else "-" for x in (name, mode, *counts)))
    # La misma llamada con spilling prohibido debe fallar antes del recoloreo.
    rejected = subprocess.run(["bin/pipoc", "mem", "tests/rename/llamada.rwt", "--rename", "--dce",
                               "--unroll", "2", "--spill", "off", "-o", OUT / "rechazado.mem"],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / "spill-off.log").write_text(rejected.stdout)
    assert rejected.returncode == 1 and "spilling esta deshabilitado" in rejected.stdout
    summary = "\n".join(rows) + "\n"
    (OUT / ("simulacion.tsv" if simulator else "compilacion.tsv")).write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sim", action="store_true")
    with simulator_script(parser.parse_args().sim) as script:
        check_cases(script)
