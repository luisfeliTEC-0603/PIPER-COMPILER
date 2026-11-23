"""Conexión entre buffers, revisión en C y presentación de errores.

El servicio solo ejecuta procesos. Este controlador decide cuándo revisar y si
una respuesta todavía pertenece al texto abierto; los widgets solo la dibujan.
"""

from pathlib import Path

from PySide6.QtCore import QObject, QTimer, Signal

from ..services.diagnostics import DiagnosticService, DiagnosticSnapshot
from ..editor.quick_fixes import get_quick_fixes, QuickFix


class DiagnosticController(QObject):
    """Revisa tras una pausa y conserva resultados independientes por documento.

    Un cambio invalida inmediatamente los errores anteriores de ese buffer.
    El temporizador de 600 ms evita ejecutar C por cada tecla y QProcess deja
    libre la interfaz. Cambiar el cursor no requiere volver a analizar nada.
    """

    message = Signal(str)

    def __init__(self, documents, editor, panel, parent=None):
        super().__init__(parent)
        self.documents, self.editor, self.panel = documents, editor, panel
        self.service = DiagnosticService(self)
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(600)
        self.timer.timeout.connect(self._request)
        self.service.ready.connect(self._ready)
        self.service.failed.connect(self._failed)
        self._snapshots = {}
        self._results = {}
        self._failures = {}
        self._generation = 0
        self._active_id = None
        self._queued = None
        self._checking = None
        self._last_failure = ""
        self._quick_fixes_cache = {}
        documents.activeChanged.connect(self.refresh)
        documents.documentsChanged.connect(self.refresh)
        editor.textChanged.connect(self.refresh)
        panel.diagnosticActivated.connect(self.navigate)

    def refresh(self):
        """Sincroniza identidades, rutas y textos antes de programar una revisión.

        Qt también emite textChanged al aplicar formatos. Se compara el texto
        real: cambiar tema o fuente no invalida un análisis ni su temporizador.
        La generación distingue dos ediciones aunque Deshacer repita el texto.
        """
        active = self.documents.active
        active_id = active.info.id if active else None
        opened = {session.info.id for session in self.documents.sessions}
        for cache in (self._snapshots, self._results, self._failures):
            for document_id in set(cache) - opened:
                del cache[document_id]
        for session in self.documents.sessions:
            info = session.info
            if info.language != "piper":
                self._snapshots.pop(info.id, None)
                self._results.pop(info.id, None)
                self._failures.pop(info.id, None)
                continue
            root = (self.documents.project.root if self.documents.project else
                    info.path.parent if info.path else Path.cwd())
            text = session.buffer.toPlainText()
            previous = self._snapshots.get(info.id)
            if previous and (previous.text, previous.source, previous.display_name,
                             previous.project_root) == (text, info.path, info.name, root):
                continue
            self._generation += 1
            self._snapshots[info.id] = DiagnosticSnapshot(
                info.id, session.buffer.revision(), self._generation,
                text, info.path, info.name, root,
            )
            self._results.pop(info.id, None)
            self._failures.pop(info.id, None)
        # La respuesta de otro buffer no debe ocupar la revisión del activo.
        # Sus resultados ya terminados sí permanecen disponibles en Problemas.
        if self._checking and (active_id != self._active_id or not self._matches(self._checking)):
            self.service.cancel()
            self._checking = None
        self._active_id = active_id
        snapshot = self._snapshots.get(active_id)
        if snapshot is None or active_id in self._results or active_id in self._failures:
            self.timer.stop()
            self._queued = None
        elif self._checking != snapshot and self._queued != snapshot:
            self._queued = snapshot
            self.timer.start()
        self._publish()

    def _matches(self, snapshot):
        """Exige identidad, generación, ruta y texto vigentes, incluso tras cerrar.

        No se exige la revisión interna actual de Qt: también puede avanzar por
        cambios de formato. La generación del texto y la copia exacta protegen
        contra respuestas viejas sin confundir resaltado con edición.
        """
        if self._snapshots.get(snapshot.document_id) != snapshot:
            return False
        session = next((item for item in self.documents.sessions
                        if item.info.id == snapshot.document_id), None)
        return bool(session and session.info.path == snapshot.source
                    and session.buffer.toPlainText() == snapshot.text)

    def _request(self):
        snapshot, self._queued = self._queued, None
        if snapshot is None or not self._matches(snapshot):
            return
        self._checking = snapshot
        self._publish()
        self.service.request(snapshot)

    def check_now(self):
        """Solicita una revisión nueva, por ejemplo después de pulsar Compilar."""
        self.refresh()
        snapshot = self._snapshots.get(self._active_id)
        if snapshot:
            self.timer.stop()
            self._results.pop(snapshot.document_id, None)
            self._failures.pop(snapshot.document_id, None)
            self._queued = snapshot
            self._request()

    def _ready(self, result):
        if not self._matches(result.snapshot):
            return
        self._checking = None
        self._results[result.snapshot.document_id] = result
        self._failures.pop(result.snapshot.document_id, None)
        self._last_failure = ""
        self._quick_fixes_cache.pop(result.snapshot.document_id, None)
        self._publish()

    def _failed(self, snapshot, message):
        if not self._matches(snapshot):
            return
        self._checking = None
        self._failures[snapshot.document_id] = message
        self._publish()
        # No repetir el mismo fallo por cada pausa de escritura ni abrir diálogos.
        if message != self._last_failure:
            self.message.emit(message)
            self._last_failure = message

    def _publish(self):
        """Publica errores vigentes de los abiertos y subraya solo el activo."""
        diagnostics = [diagnostic for session in self.documents.sessions
                       if (result := self._results.get(session.info.id))
                       for diagnostic in result.diagnostics]
        result = self._results.get(self._active_id)

        active_fixes_map = {}
        if result:
            if self._active_id not in self._quick_fixes_cache:
                self._quick_fixes_cache[self._active_id] = {}
            cache = self._quick_fixes_cache[self._active_id]
            source_text = result.snapshot.text
            for diagnostic in result.diagnostics:
                # Cachear por (code, message): dos instancias del mismo error
                # reutilizan la misma lista de fixes, y aplicar uno no invalida
                # la caché del otro. La clave es estable entre revisiones.
                key = (diagnostic.code, diagnostic.message)
                if key not in cache:
                    cache[key] = get_quick_fixes(source_text, diagnostic)
                active_fixes_map[id(diagnostic)] = cache[key]
        else:
            # Si no hay resultado, limpiar la caché del buffer activo para
            # no conservar fixes de una revisión anterior.
            if self._active_id is not None:
                self._quick_fixes_cache.pop(self._active_id, None)

        self.editor.set_diagnostics(
            result.diagnostics if result else (),
            active_fixes_map,
        )

        snapshot = self._snapshots.get(self._active_id)
        if snapshot is None:
            status = "La revisión de errores está disponible para archivos Piper (.rwt)."
        elif self._active_id in self._failures:
            status = f"{snapshot.display_name}: revisión no disponible. {self._failures[self._active_id]}"
        elif result:
            count = len(result.diagnostics)
            noun = "error" if count == 1 else "errores"
            status = (f"{snapshot.display_name}: {count} {noun}." if count else
                      f"{snapshot.display_name}: sin errores léxicos, sintácticos ni semánticos.")
        else:
            action = "Revisando" if self._checking == snapshot else "Pendiente de revisión"
            status = f"{action}: {snapshot.display_name}…"
        self.panel.set_diagnostics(diagnostics, status)

    def navigate(self, diagnostic):
        """Abre el buffer del error antes de mover el cursor; nunca usa el activo a ciegas."""
        result = self._results.get(diagnostic.document_id)
        if not result or diagnostic not in result.diagnostics or not self._matches(result.snapshot):
            return
        self.documents.activate(diagnostic.document_id)
        if diagnostic.has_location:
            self.editor.go_to_position(diagnostic.line, diagnostic.column)

    def stop(self):
        """Detiene revisiones antes de que se destruyan documentos y widgets."""
        self.timer.stop()
        self.service.stop()
