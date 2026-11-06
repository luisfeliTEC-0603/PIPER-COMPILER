"""Coordina la detección de clones en todos los archivos del proyecto.

Corre `pipoc --clones` dos veces por archivo (modo exact y renamed) para
poder clasificar los grupos como Tipo I (exactos) o Tipo II (renombrados
puros). La clasificación se hace cruzando hashes y rangos: un grupo
renamed que comparte hash con un grupo exact, o cuyas ubicaciones están
contenidas en las de un grupo exact, se descarta como duplicado.
"""

from collections import defaultdict
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from ..models.clone import CloneGroup, CloneLocation, CloneReport
from ..services.clone_detector import CloneDetectorService


class CloneController(QObject):
    """Ejecuta la detección bajo demanda y publica el reporte consolidado."""

    message = Signal(str)
    detection_started = Signal()
    detection_finished = Signal(object)

    def __init__(self, documents, editor, panel, parent=None):
        super().__init__(parent)
        self.documents = documents
        self.editor = editor
        self.panel = panel
        self.service = CloneDetectorService(self)
        self.service.ready.connect(self._file_ready)
        self.service.failed.connect(self._file_failed)

        self._queue: list[tuple[Path, Path, str]] = []   # (archivo, root, modo)
        self._results: dict[str, list[CloneReport]] = {"exact": [], "renamed": []}
        self._done: int = 0
        self._total: int = 0
        self._files_in_run: int = 0
        self._min_nodes: int = 8
        self._running: bool = False

        panel.detectRequested.connect(self.detect)
        panel.cancelRequested.connect(self.cancel)
        panel.cloneActivated.connect(self.navigate)

    # --- API pública ---------------------------------------------------

    def detect(self, min_nodes: int = 8):
        """Inicia una detección nueva sobre todos los archivos del proyecto."""
        if self._running:
            return
        self._min_nodes = max(1, int(min_nodes))
        files = self._collect_files()
        if not files:
            self.message.emit("No hay archivos .rwt abiertos ni en el proyecto.")
            self.panel.set_report(None, "No hay archivos .rwt para analizar.")
            return
        self._queue = []
        for path, root in files:
            self._queue.append((path, root, "exact"))
            self._queue.append((path, root, "renamed"))
        self._results = {"exact": [], "renamed": []}
        self._done = 0
        self._total = len(self._queue)
        self._files_in_run = len(files)
        self._running = True
        self.panel.set_running(True)
        self.detection_started.emit()
        self.message.emit(
            f"Analizando {len(files)} archivo(s) en modos exact y renamed…"
        )
        self._next()

    def cancel(self):
        if not self._running:
            return
        self.service.cancel()
        self._queue.clear()
        self._results = {"exact": [], "renamed": []}
        self._running = False
        self.panel.set_running(False)
        self.message.emit("Detección de clones cancelada.")
        self.panel.set_report(None, "Detección cancelada.")

    def stop(self):
        self.service.stop()

    # --- Navegación ----------------------------------------------------

    def navigate(self, group: CloneGroup, location: CloneLocation):
        try:
            if location.file_path and location.file_path.exists():
                self.documents.open_path(location.file_path)
        except Exception:
            pass
        self.editor.go_to_position(location.start_line, location.start_column)

    # --- Flujo interno -------------------------------------------------

    def _collect_files(self) -> list[tuple[Path, Path]]:
        seen: set[Path] = set()
        result: list[tuple[Path, Path]] = []
        project_root = (
            self.documents.project.root if self.documents.project else Path.cwd()
        )
        for session in self.documents.sessions:
            if session.info.language != "piper" or not session.info.path:
                continue
            path = session.info.path.resolve()
            if path in seen:
                continue
            seen.add(path)
            result.append((path, project_root))
        if self.documents.project and self.documents.project.root:
            for path in self.documents.project.root.rglob("*.rwt"):
                resolved = path.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    result.append((resolved, project_root))
        return result

    def _next(self):
        if not self._queue:
            self._finish()
            return
        if not self._running:
            return
        path, root, mode = self._queue.pop(0)
        started = self.service.request(path, root, self._min_nodes, mode)
        if not started:
            self._done += 1
            self._next()

    def _file_ready(self, source: str, mode: str, report: CloneReport):
        self._done += 1
        self._results[mode].append(report)
        self.message.emit(
            f"Analizado {Path(source).name} [{mode}] "
            f"({self._done}/{self._total})"
        )
        self._next()

    def _file_failed(self, source: str, mode: str, message: str):
        self._done += 1
        self.message.emit(f"{Path(source).name} [{mode}]: {message}")
        self._next()

    def _finish(self):
        self._running = False
        self.panel.set_running(False)
        consolidated = self._consolidate()
        self.detection_finished.emit(consolidated)
        self.panel.set_report(consolidated, self._summary(consolidated))
        self._publish_highlights(consolidated)
        self._results = {"exact": [], "renamed": []}
        self._queue = []

    def _consolidate(self) -> CloneReport:
        """Agrupa por hash, clasifica en Tipo I / Tipo II y filtra contenidos.

        - Los grupos del modo exact se reportan como Tipo I.
        - Los grupos del modo renamed cuyo hash NO esté en exact se reportan
          como Tipo II, salvo que sus ubicaciones estén contenidas en las de
          un grupo Tipo I (en cuyo caso son la misma copia vista a mayor
          granularidad y se descartan).
        """
        exact_by_hash: dict[str, list[CloneLocation]] = defaultdict(list)
        renamed_by_hash: dict[str, list[CloneLocation]] = defaultdict(list)
        for report in self._results["exact"]:
            for group in report.groups:
                for location in group.locations:
                    exact_by_hash[group.hash].append(location)
        for report in self._results["renamed"]:
            for group in report.groups:
                for location in group.locations:
                    renamed_by_hash[group.hash].append(location)

        type_i: list[CloneGroup] = []
        for hash_value, locations in exact_by_hash.items():
            if len(locations) < 2:
                continue
            locations.sort(key=lambda l: (str(l.file_path), l.start_line, l.start_column))
            type_i.append(CloneGroup(
                mode="exact",
                hash=hash_value,
                locations=tuple(locations),
            ))

        type_ii: list[CloneGroup] = []
        for hash_value, locations in renamed_by_hash.items():
            if hash_value in exact_by_hash:
                continue  # mismo hash que un Tipo I: es el mismo clon
            if len(locations) < 2:
                continue
            if self._all_contained(locations, exact_by_hash):
                continue  # es la misma copia a mayor granularidad
            locations.sort(key=lambda l: (str(l.file_path), l.start_line, l.start_column))
            type_ii.append(CloneGroup(
                mode="renamed",
                hash=hash_value,
                locations=tuple(locations),
            ))

        groups = type_i + type_ii
        groups.sort(key=lambda g: (g.clone_type, -g.size, g.hash))
        files_analyzed = len({
            loc.file_path for g in groups for loc in g.locations
        })
        return CloneReport(
            groups=groups,
            files_analyzed=files_analyzed,
            mode="mixed",
            min_nodes=self._min_nodes,
        )

    def _all_contained(self, locations, exact_by_hash) -> bool:
        """Indica si todas las ubicaciones están contenidas en algún Tipo I."""
        for location in locations:
            contained = False
            for exact_locations in exact_by_hash.values():
                for other in exact_locations:
                    if (other.file_path == location.file_path
                            and self._range_contains(other, location)):
                        contained = True
                        break
                if contained:
                    break
            if not contained:
                return False
        return True

    @staticmethod
    def _range_contains(outer: CloneLocation, inner: CloneLocation) -> bool:
        """Mismo criterio que `range_contains` del detector en C."""
        if outer == inner:
            return False
        return (
            (outer.start_line < inner.start_line
             or (outer.start_line == inner.start_line
                 and outer.start_column <= inner.start_column))
            and (outer.end_line > inner.end_line
                 or (outer.end_line == inner.end_line
                     and outer.end_column >= inner.end_column))
        )

    def _summary(self, report: CloneReport) -> str:
        if not report.groups:
            return "No se encontraron clones."
        type_i = [g for g in report.groups if g.clone_type == "I"]
        type_ii = [g for g in report.groups if g.clone_type == "II"]
        parts = []
        if type_i:
            parts.append(f"{len(type_i)} grupo(s) Tipo I")
        if type_ii:
            parts.append(f"{len(type_ii)} grupo(s) Tipo II")
        detail = " y ".join(parts)
        return (
            f"{detail}; "
            f"{report.total_locations} ubicaciones en "
            f"{report.files_analyzed} archivo(s)."
        )

    def _publish_highlights(self, report: CloneReport):
        active = self.documents.active
        if not active or not active.info.path:
            if hasattr(self.editor, "set_clone_highlights"):
                self.editor.set_clone_highlights([])
            return
        active_path = active.info.path.resolve()
        relevant = [
            group for group in report.groups
            if any(loc.file_path.resolve() == active_path for loc in group.locations)
        ]
        if hasattr(self.editor, "set_clone_highlights"):
            self.editor.set_clone_highlights(relevant)
