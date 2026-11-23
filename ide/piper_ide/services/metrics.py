"""Medición de dos configuraciones sobre el mismo fuente y el mismo RTL."""

from dataclasses import asdict
import json
import os
from pathlib import Path
import signal
import subprocess
from tempfile import TemporaryDirectory, mkdtemp
from time import perf_counter

from PySide6.QtCore import QThread, Signal

from ..models.compilation import OptimizationOptions
from .compiler import CompilerConfigurationError, find_compiler


def simulator_for(compiler: Path, selected: str) -> Path | None:
    siblings = compiler.parent.parent.parent
    roots = [Path(selected)] if selected else [
        siblings / "PORYGON",
        siblings.parent / "ProyectoArqui" / "PORYGON",
    ]
    return next((root / "tools/sim.sh" for root in roots if (root / "tools/sim.sh").is_file()), None)


class Measurement(QThread):
    progress = Signal(str)
    ready = Signal(dict)
    failed = Signal(str)

    def __init__(self, source, root, options, simulator_root, parent=None):
        super().__init__(parent)
        self.source = source.resolve()
        self.root = root.resolve()
        self.options = options
        self.simulator_root = simulator_root

    def command(self, args):
        with subprocess.Popen(args, cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", start_new_session=True) as process:
            while True:
                try:
                    output, _ = process.communicate(timeout=0.2)
                    break
                except subprocess.TimeoutExpired:
                    if self.isInterruptionRequested():
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        raise InterruptedError("Medición cancelada.")
        if process.returncode:
            raise ValueError(output.strip()[-1600:] or f"El proceso terminó con código {process.returncode}.")

    def run(self):
        try:
            self.measure()
        except (CompilerConfigurationError, OSError, ValueError) as error:
            self.failed.emit(str(error))

    def measure(self):
        compiler = find_compiler(self.root)
        simulator = simulator_for(compiler, self.simulator_root)
        report = {"source": str(self.source), "simulator": str(simulator) if simulator else None,
                  "variants": {}, "warning": ""}
        with TemporaryDirectory(prefix="piper-metricas-") as temp:
            workspace = Path(temp)
            snapshot = workspace / self.source.name
            snapshot.write_bytes(self.source.read_bytes())
            for name, options in (("Base", OptimizationOptions()), ("Selección", self.options)):
                if self.isInterruptionRequested():
                    raise InterruptedError("Medición cancelada.")
                directory = workspace / ("base" if name == "Base" else "selected")
                directory.mkdir()
                variant = {"options": asdict(options), "values": {}, "error": ""}
                report["variants"][name] = variant
                try:
                    self.progress.emit(f"Compilando · {name}")
                    args = [compiler, "mem", snapshot, "-I", self.source.parent, "-o", directory / "code.mem",
                            "--emit-metrics", directory / "static.json", "--emit-bundles",
                            directory / "code.bundles", *options.arguments]
                    start = perf_counter()
                    self.command(args)
                    elapsed = (perf_counter() - start) * 1000
                    values = json.loads((directory / "static.json").read_text())
                    for key in ("instructions", "code_bytes", "removed", "reordered", "renamed"):
                        if type(values.get(key)) is not int or values[key] < 0:
                            raise ValueError(f"Contador de pipoc inválido: {key}")
                    variant["values"] = {**values, "compile_ms": elapsed}
                    if not simulator:
                        raise ValueError("Elegí la carpeta de PORYGON y repetí la medición para obtener ciclos.")
                    self.progress.emit(f"Simulando · {name}")
                    cycles, returned = self.simulate(simulator, directory)
                    variant["values"]["cycles"] = cycles
                    variant["r1"] = returned
                except InterruptedError:
                    raise
                except (OSError, ValueError, KeyError, TypeError) as error:
                    variant["error"] = str(error)
        base, selected = report["variants"].values()
        if "r1" in base and "r1" in selected and base["r1"] != selected["r1"]:
            report["warning"] = f"Resultados distintos en r1: Base {base['r1']} y Selección {selected['r1']}."
        output = self.root / "build/ide"
        output.mkdir(parents=True, exist_ok=True)
        path = Path(mkdtemp(prefix=f"{self.source.stem}-metricas-", dir=output)) / "metricas.json"
        report["path"] = str(path)
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        self.ready.emit(report)

    def simulate(self, simulator, directory):
        labels = (directory / "code.bundles").read_text().splitlines()
        halt = next((line.split()[2] for line in labels if line.startswith("LABEL piper_fin_programa ")), None)
        if halt is None:
            raise ValueError("No se encontró la etiqueta de terminación normal.")
        expected_pc = int(halt) * 8
        try:
            self.command(["bash", simulator, directory / "code.mem", "--out", directory,
                          "--expect", "halted=1", "--expect", "errors=0",
                          "--expect", f"halt_pc={expected_pc}"])
        except ValueError as error:
            if not (directory / "results.txt").exists():
                raise
            result = self.simulator_result(directory)
            if result.get("timeout") == "1":
                raise ValueError("El simulador agotó el límite de ciclos.") from error
            if result.get("errors") != "0":
                raise ValueError(f"El RTL registró errores: {result.get('errors', '?')}.") from error
            raise ValueError(f"Sin terminación normal. PC de parada {result.get('halt_pc', '?')}, "
                             f"PC esperado {expected_pc:#x}.") from error
        indicators = json.loads((directory / "indicators.json").read_text())
        result = self.simulator_result(directory)
        cycles = indicators["contadores"]["cpu_cycles"]
        if not indicators["comparable"] or cycles != int(result["cycles"]):
            raise ValueError("Los contadores del simulador no son comparables.")
        return cycles, int(result["r1"], 0)

    @staticmethod
    def simulator_result(directory):
        return dict(line.split("=", 1) for line in (directory / "results.txt").read_text().splitlines()
                    if "=" in line)
