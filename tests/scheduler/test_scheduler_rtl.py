"""Prueba opcional contra un checkout local de PORYGON, sin modificar ese repositorio."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("porygon", type=Path, help="Raíz del repositorio de arquitectura")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    rtl = args.porygon.resolve() / "hardware" / "src"
    if not (rtl / "pkg_vliw.sv").is_file():
        parser.error("La ruta debe contener hardware/src/pkg_vliw.sv")

    with tempfile.TemporaryDirectory(prefix="porygon-scheduler-") as temporary:
        work = Path(temporary)
        image = work / "program.mem"
        subprocess.run([
            str(root / "bin/porygon-schedule"),
            str(root / "tests/scheduler/control_memory.asm"),
            str(work / "program.bundles"), str(image),
        ], check=True)
        bundles = len(image.read_text().splitlines())
        sources = [rtl / "pkg_vliw.sv"]
        sources += sorted(p for p in rtl.glob("*.sv") if p.name != "pkg_vliw.sv")
        sources += sorted((rtl / "idd").glob("*.sv"))
        sources += sorted((rtl / "crypto").glob("*.sv"))
        executable = work / "scheduler.vvp"
        command = [
            "iverilog", "-g2012", "-s", "tb_scheduler",
            f'-Ptb_scheduler.IMAGE="{image}"', f"-Ptb_scheduler.BUNDLES={bundles}",
            "-o", str(executable), *map(str, sources), str(root / "tests/scheduler/test_scheduler_rtl.sv"),
        ]
        compilation = subprocess.run(command, capture_output=True, text=True)
        if compilation.returncode:
            print(compilation.stdout, end="")
            print(compilation.stderr, end="")
            compilation.check_returncode()
        subprocess.run(["vvp", str(executable)], check=True, cwd=work)


if __name__ == "__main__":
    main()
