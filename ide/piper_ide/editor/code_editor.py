from html import escape
import re

from PySide6.QtCore import QEvent, QRect, Qt, Signal
from PySide6.QtGui import (
    QAction, QColor, QPainter, QPen, QTextCharFormat, QTextCursor,
    QTextDocument, QTextFormat,
)
from PySide6.QtWidgets import QPlainTextEdit, QTextEdit, QToolTip, QWidget, QMenu

from ..ui.theme import monospace_font


class _LineNumbers(QWidget):
    """Margen hijo del editor: delega el dibujo para compartir sus coordenadas."""

    def paintEvent(self, event):
        """Solicita al editor los números de los bloques actualmente visibles."""
        self.parent().paint_line_numbers(event)


class CodeEditor(QPlainTextEdit):

    completionRequested = Signal()
    completionTriggered = Signal()

    _AUTO_CLOSE_PAIRS = {
        "{": "}",
        "(": ")",
        "[": "]",
    }

    def __init__(self, parent=None):
        """Configura texto sin ajuste de línea y sincroniza el margen con Qt."""
        super().__init__(parent)
        self.setObjectName("codeEditor")
        self.setAccessibleName("Editor de código")
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.line_numbers = _LineNumbers(self)
        self._colors = {}
        self._search_matches = []
        self._diagnostic_ranges = []
        self._diagnostic_lines = set()
        self._quick_fixes = {}
        self.viewport().setMouseTracking(True)
        self.set_code_font_size(14)
        self.blockCountChanged.connect(self._update_margin)
        self.updateRequest.connect(self._update_numbers)
        self.cursorPositionChanged.connect(self._highlight_current_line)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._show_context_menu)
        self._update_margin()

    def set_code_font_size(self, size: int):
        """Cambia el tamaño en píxeles y recalcula tabulaciones de cuatro espacios.

        El ancho de los números depende de la fuente, por lo que también se
        recalcula el margen. No se sustituye el documento ni su historial.
        """
        self.setFont(monospace_font(size))
        self.setTabStopDistance(self.fontMetrics().horizontalAdvance(" ") * 4)
        self._update_margin()
        self.viewport().update()

    def apply_theme(self, colors: dict[str, str]):
        """Recibe la paleta y repinta las partes dibujadas fuera de QSS."""
        self._colors = colors
        self._highlight_current_line()
        self.line_numbers.update()
        self.viewport().update()

    def _margin_width(self):
        """Calcula el ancho en píxeles según la cantidad de dígitos de las líneas."""
        digits = len(str(max(1, self.blockCount())))
        return 26 + self.fontMetrics().horizontalAdvance("9") * max(2, digits)

    def _update_margin(self, *_):
        """Reserva espacio para los números tras cambiar la fuente o las líneas."""
        self.setViewportMargins(self._margin_width(), 0, 0, 0)
        self._position_numbers()

    def _position_numbers(self):
        """Alinea el widget del margen con el área interior del editor."""
        rect = self.contentsRect()
        self.line_numbers.setGeometry(QRect(rect.left(), rect.top(), self._margin_width(), rect.height()))

    def _update_numbers(self, rect, dy):
        """Acompaña el desplazamiento vertical y repinta solo el área afectada."""
        if dy:
            self.line_numbers.scroll(0, dy)
        else:
            self.line_numbers.update(0, rect.y(), self.line_numbers.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_margin()

    def resizeEvent(self, event):
        """Reposiciona el margen después del cambio de tamaño manejado por Qt."""
        super().resizeEvent(event)
        self._position_numbers()

    def _highlight_current_line(self):
        """Dibuja el fondo de la línea activa y las decoraciones de diagnóstico."""
        if not self._colors:
            return
        selection = QTextEdit.ExtraSelection()
        selection.format.setBackground(QColor(self._colors["surface"]))
        selection.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        selection.cursor = self.textCursor()
        selection.cursor.clearSelection()
        selections = [selection]
        for match in self._search_matches:
            highlight = QTextEdit.ExtraSelection()
            highlight.cursor = match
            highlight.format.setBackground(QColor(self._colors["raised"]))
            selections.append(highlight)
        for cursor, _diagnostic in self._diagnostic_ranges:
            underline = QTextEdit.ExtraSelection()
            underline.cursor = cursor
            underline.format.setBackground(QColor(self._colors["error-soft"]))
            underline.format.setUnderlineStyle(QTextCharFormat.UnderlineStyle.WaveUnderline)
            underline.format.setUnderlineColor(QColor(self._colors["error"]))
            selections.append(underline)
        self.setExtraSelections(selections)

    def setDocument(self, document):
        """Cambia de buffer y elimina decoraciones que pertenecían al anterior."""
        self._search_matches = []
        self._diagnostic_ranges = []
        self._diagnostic_lines = set()
        self._quick_fixes = {}
        QToolTip.hideText()
        super().setDocument(document)
        self._update_margin()
        self._highlight_current_line()

    def _text_position(self, line: int, column: int) -> int:
        """Convierte una columna Unicode de Python a una posición UTF-16 de Qt."""
        block = self.document().findBlockByNumber(max(0, min(line, self.blockCount() - 1)))
        prefix = block.text()[:max(0, column)]
        return block.position() + len(prefix.encode("utf-16-le")) // 2

    def set_diagnostics(self, diagnostics, quick_fixes=None):
        """Decora rangos del buffer actual sin cambiar texto, selección ni Deshacer.

        Un rango vacío representa un símbolo ausente. Se conserva vacío para
        dibujar una marca de inserción en paintEvent, sin subrayar un carácter
        correcto ni desplazar la navegación al token anterior o siguiente.

        quick_fixes es un dict {id(diagnostic): [QuickFix, ...]}, opcional, que
        asocia cada diagnóstico con sus correcciones rápidas disponibles.
        """
        self._diagnostic_ranges = []
        self._diagnostic_lines = set()
        self._quick_fixes = quick_fixes or {}
        for diagnostic in diagnostics:
            if not diagnostic.has_location:
                continue
            start = self._text_position(diagnostic.line, diagnostic.column)
            end = self._text_position(
                diagnostic.end_line if diagnostic.end_line is not None else diagnostic.line,
                diagnostic.end_column if diagnostic.end_column is not None else diagnostic.column,
            )
            cursor = QTextCursor(self.document())
            cursor.setPosition(start)
            if end > start:
                cursor.setPosition(end, QTextCursor.MoveMode.KeepAnchor)
            self._diagnostic_ranges.append((cursor, diagnostic))
            self._diagnostic_lines.add(diagnostic.line)
        self._highlight_current_line()
        self.line_numbers.update()
        self.viewport().update()
        QToolTip.hideText()

    def diagnostic_tooltip(self, position: int) -> str:
        """Construye texto seguro para el tooltip de los errores bajo una posición."""
        messages = [f"{escape(d.phase_label)}: {escape(d.message)}"
                    for cursor, d in self._diagnostic_ranges
                    if cursor.selectionStart() <= position < cursor.selectionEnd()
                    or cursor.selectionStart() == cursor.selectionEnd() == position]
        return "<qt>" + "<br>".join(dict.fromkeys(messages)) + "</qt>" if messages else ""

    def viewportEvent(self, event):
        """Muestra el diagnóstico al pasar el mouse por su rango subrayado."""
        if event.type() == QEvent.Type.ToolTip:
            text = self.diagnostic_tooltip(self.cursorForPosition(event.pos()).position())
            if text:
                QToolTip.showText(event.globalPos(), text, self.viewport())
            else:
                QToolTip.hideText()
                event.ignore()
            return True
        return super().viewportEvent(event)

    def _show_context_menu(self, pos):
        """Construye un menú contextual estándar y añade correcciones rápidas.

        Si el clic cae sobre un diagnóstico (o su marca de inserción), se
        añaden las acciones de ``QuickFix`` disponibles para ese diagnóstico.
        """
        menu = self.createStandardContextMenu()
        click_cursor = self.cursorForPosition(pos)
        click_pos = click_cursor.position()

        diagnostic_under_cursor = None
        for diag_cursor, diagnostic in self._diagnostic_ranges:
            if diag_cursor.hasSelection():
                if diag_cursor.selectionStart() <= click_pos <= diag_cursor.selectionEnd():
                    diagnostic_under_cursor = diagnostic
                    break
            else:
                if abs(diag_cursor.position() - click_pos) <= 1:
                    diagnostic_under_cursor = diagnostic
                    break
        if diagnostic_under_cursor is not None:
            fixes = self._quick_fixes.get(id(diagnostic_under_cursor), [])
            if fixes:
                menu.addSeparator()
                for fix in fixes:
                    action = QAction(fix.title, menu)
                    action.triggered.connect(
                        lambda checked=False, f=fix: self._apply_quick_fix(f)
                    )
                    menu.addAction(action)

        menu.exec(self.mapToGlobal(pos))

    def _apply_quick_fix(self, fix):
        """Aplica una corrección rápida como una única operación de Deshacer."""
        cursor = QTextCursor(self.document())
        start_pos = self._text_position(fix.line, fix.column)

        cursor.beginEditBlock()
        if fix.end_line is not None and fix.end_column is not None:
            end_pos = self._text_position(fix.end_line, fix.end_column)
            cursor.setPosition(start_pos)
            cursor.setPosition(end_pos, QTextCursor.MoveMode.KeepAnchor)
            cursor.insertText(fix.replacement)
            final_pos = start_pos + len(fix.replacement)
        else:
            cursor.setPosition(start_pos)
            cursor.insertText(fix.replacement)
            final_pos = start_pos + len(fix.replacement)

        # Si el reemplazo contiene el marcador de snippet $0, colocar el cursor
        # allí y eliminar el marcador del texto real del documento.
        if fix.is_snippet and "$0" in fix.replacement:
            offset = fix.replacement.find("$0")
            marker_start = start_pos + offset
            cleanup = QTextCursor(self.document())
            cleanup.setPosition(marker_start)
            cleanup.setPosition(marker_start + 2, QTextCursor.MoveMode.KeepAnchor)
            cleanup.removeSelectedText()
            final_pos = marker_start

        cursor.endEditBlock()
        final_cursor = QTextCursor(self.document())
        final_cursor.setPosition(final_pos)
        self.setTextCursor(final_cursor)

    def search_flags(self, case_sensitive=False, whole_words=False):
        """Construye las opciones nativas de búsqueda literal, sin expresiones regulares."""
        flags = QTextDocument.FindFlag(0)
        if case_sensitive:
            flags |= QTextDocument.FindFlag.FindCaseSensitively
        if whole_words:
            flags |= QTextDocument.FindFlag.FindWholeWords
        return flags

    def highlight_search(self, query: str, flags) -> tuple[int, bool]:
        """Destaca hasta mil coincidencias y devuelve (cantidad, hay_más)."""
        self._search_matches = []
        cursor = QTextCursor(self.document())
        if query:
            while len(self._search_matches) < 1000:
                cursor = self.document().find(query, cursor, flags)
                if cursor.isNull():
                    break
                self._search_matches.append(cursor)
        more = bool(query and not cursor.isNull() and not self.document().find(query, cursor, flags).isNull())
        self._highlight_current_line()
        return len(self._search_matches), more

    def find_text(self, query: str, flags, backwards=False) -> bool:
        """Selecciona la próxima coincidencia, continuando al inicio/final al llegar al borde."""
        if not query:
            return False
        if backwards:
            flags |= QTextDocument.FindFlag.FindBackward
        found = self.document().find(query, self.textCursor(), flags)
        if found.isNull():
            start = QTextCursor(self.document())
            start.movePosition(QTextCursor.MoveOperation.End if backwards else QTextCursor.MoveOperation.Start)
            found = self.document().find(query, start, flags)
        if found.isNull():
            return False
        self.setTextCursor(found)
        self.ensureCursorVisible()
        return True

    def go_to_line(self, line: int):
        """Sitúa el cursor al inicio de una línea desde uno, limitada al documento."""
        block = self.document().findBlockByNumber(max(0, min(line - 1, self.blockCount() - 1)))
        self.setTextCursor(QTextCursor(block))
        self.centerCursor()

    def go_to_position(self, line: int, column: int):
        """Navega a una ubicación basada en cero reportada por pipoc."""
        cursor = QTextCursor(self.document())
        cursor.setPosition(self._text_position(line, column))
        self.setTextCursor(cursor)
        self.centerCursor()
        self.setFocus()

    def _indent(self, backwards=False):
        """Indenta/desindenta líneas en una sola operación de Deshacer."""
        cursor = self.textCursor()
        if not cursor.hasSelection() and not backwards:
            cursor.insertText(" " * (4 - cursor.positionInBlock() % 4))
            return
        first = self.document().findBlock(cursor.selectionStart()).blockNumber()
        last_position = max(cursor.selectionStart(), cursor.selectionEnd() - 1)
        last = self.document().findBlock(last_position).blockNumber()
        selected = cursor.hasSelection()
        edit = QTextCursor(cursor)
        edit.beginEditBlock()
        for number in range(last, first - 1, -1):
            block = self.document().findBlockByNumber(number)
            edit.setPosition(block.position())
            if backwards:
                leading = re.match(r"(?:\t| {1,4})", block.text())
                if leading:
                    edit.setPosition(block.position() + len(leading.group()), QTextCursor.MoveMode.KeepAnchor)
                    edit.removeSelectedText()
            else:
                edit.insertText(" " * 4)
        edit.endEditBlock()
        if selected:
            cursor.setPosition(self.document().findBlockByNumber(first).position())
            end = self.document().findBlockByNumber(last)
            cursor.setPosition(end.position() + end.length() - 1, QTextCursor.MoveMode.KeepAnchor)
        self.setTextCursor(cursor)

    def _inside_block_comment(self, position: int) -> bool:
        """Indica si ``position`` está dentro de un comentario ``*** ... ***``"""
        text = self.toPlainText()[:position]
        return text.count("***") % 2 == 1

    def _insert_pair(self, opening: str, closing: str) -> None:
        """Inserta ``opening + closing`` y deja el cursor entre ambos."""
        cursor = self.textCursor()
        selected = cursor.selectedText()
        cursor.beginEditBlock()
        if selected:
            cursor.insertText(opening + selected + closing)
            cursor.setPosition(cursor.position() - len(closing))
        else:
            cursor.insertText(opening + closing)
            cursor.movePosition(QTextCursor.MoveOperation.PreviousCharacter,
                                QTextCursor.MoveMode.MoveAnchor, len(closing))
        cursor.endEditBlock()
        self.setTextCursor(cursor)

    def _handle_auto_close(self, typed: str) -> bool:
        """Intenta aplicar auto-cierre para el texto recién tecleado.

        Retorna ``True`` si la pulsación fue consumida; ``False`` si el editor
        debe procesar la tecla de forma normal."""

        cursor = self.textCursor()

        # Skip over: no duplicar cierres 
        if typed in (")", "]", "}") and not cursor.hasSelection():
            block = cursor.block()
            position_in_block = cursor.positionInBlock()
            if (position_in_block < len(block.text())
                    and block.text()[position_in_block] == typed):
                cursor.movePosition(QTextCursor.MoveOperation.NextCharacter)
                self.setTextCursor(cursor)
                return True

        # Delimitador ``|`` para dimensiones de arreglos
        if typed == "|":
            block_text = cursor.block().text()
            pos_in_block = cursor.positionInBlock()
            if (not cursor.hasSelection()
                    and pos_in_block < len(block_text)
                    and block_text[pos_in_block] == "|"):
                cursor.movePosition(QTextCursor.MoveOperation.NextCharacter)
                self.setTextCursor(cursor)
                return True
            self._insert_pair("|", "|")
            return True

        # Comentario de bloque ``***``
        if typed == "*":
            block_text = cursor.block().text()
            pos_in_block = cursor.positionInBlock()
            if (pos_in_block >= 2 and block_text[pos_in_block - 2:pos_in_block] == "**"
                    and not cursor.hasSelection()):
                if not self._inside_block_comment(cursor.position()):
                    cursor.beginEditBlock()
                    cursor.insertText("* ")
                    cursor.setPosition(cursor.position() + 1)
                    cursor.insertText("***")
                    cursor.setPosition(cursor.position() - 4)
                    cursor.endEditBlock()
                    self.setTextCursor(cursor)
                    return True
                return False
            return False

        # Pares clásicos ``{}``, ``()``, ``[]``
        closing = self._AUTO_CLOSE_PAIRS.get(typed)
        if closing is not None:
            self._insert_pair(typed, closing)
            return True

        return False

    def keyPressEvent(self, event):
        """Mantiene la edición directa y añade indentación y auto-cierre de pares."""
        if (event.modifiers() == Qt.KeyboardModifier.ControlModifier
                and event.key() == Qt.Key.Key_Space):
            self.completionRequested.emit()
            return
        if self.isReadOnly() or event.modifiers() & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier):
            super().keyPressEvent(event)
            return

        if self._handle_auto_close(event.text()):
            return

        if event.key() in (Qt.Key.Key_Tab, Qt.Key.Key_Backtab):
            self._indent(event.key() == Qt.Key.Key_Backtab or bool(event.modifiers() & Qt.KeyboardModifier.ShiftModifier))
            return
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            cursor = self.textCursor()
            before = QTextCursor(cursor)
            before.setPosition(cursor.selectionStart())
            before.movePosition(QTextCursor.MoveOperation.StartOfBlock, QTextCursor.MoveMode.KeepAnchor)
            prefix = before.selectedText()
            indent = re.match(r"[ \t]*", prefix).group()
            if prefix.rstrip().endswith("{"):
                indent += " " * 4
            cursor.insertText("\n" + indent)
            self.setTextCursor(cursor)
            return
        typed = event.text()
        super().keyPressEvent(event)
        if typed in {"[", ",", "&"}:
            # Estos delimitadores abren o continúan construcciones de Piper;
            # el controlador consulta la tabla aunque aún no exista fragmento.
            self.completionTriggered.emit()

    def _visible_blocks(self, bottom):
        """Produce (bloque, y superior, altura) hasta el límite inferior en píxeles."""
        block = self.firstVisibleBlock()
        while block.isValid():
            top = self.blockBoundingGeometry(block).translated(self.contentOffset()).top()
            height = self.blockBoundingRect(block).height()
            if top > bottom:
                break
            if block.isVisible() and top + height >= 0:
                yield block, int(top), int(height)
            block = block.next()

    def paint_line_numbers(self, event):
        """Pinta el fondo y la numeración desde uno en el margen independiente."""
        if not self._colors:
            return
        painter = QPainter(self.line_numbers)
        painter.fillRect(event.rect(), QColor(self._colors["sidebar"]))
        painter.setFont(self.font())
        painter.setPen(QColor(self._colors["border-soft"]))
        painter.drawLine(self.line_numbers.width() - 1, event.rect().top(),
                         self.line_numbers.width() - 1, event.rect().bottom())
        active_block = self.textCursor().blockNumber()
        for block, top, height in self._visible_blocks(event.rect().bottom()):
            has_error = block.blockNumber() in self._diagnostic_lines
            if has_error:
                painter.fillRect(3, top + 2, 3, max(1, height - 4), QColor(self._colors["error"]))
            painter.setPen(QColor(
                self._colors["error"] if has_error else
                self._colors["accent"] if block.blockNumber() == active_block
                else self._colors["dim"]
            ))
            painter.drawText(0, top, self.line_numbers.width() - 12, height,
                             Qt.AlignmentFlag.AlignRight, str(block.blockNumber() + 1))

    def paintEvent(self, event):
        """Deja a Qt dibujar el texto y superpone guías y marcas de inserción."""
        super().paintEvent(event)
        if not self._colors:
            return
        painter = QPainter(self.viewport())
        painter.setPen(QColor(self._colors["border-soft"]))
        space = self.fontMetrics().horizontalAdvance(" ")
        for block, top, height in self._visible_blocks(event.rect().bottom()):
            text = block.text()
            leading = len(text) - len(text.lstrip(" \t"))
            columns = len(text[:leading].expandtabs(4))
            origin = self.blockBoundingGeometry(block).translated(self.contentOffset()).left()
            for column in range(0, columns, 4):
                x = int(origin + self.document().documentMargin() + column * space)
                painter.drawLine(x, top, x, top + height)
        # Los símbolos que faltan no tienen texto que colorear. Una marca fija
        # de dos píxeles señala su posición exacta, incluso al final del archivo
        # o antes de un comentario. Es una decoración: no inserta caracteres.
        painter.setPen(QPen(QColor(self._colors["error"]), 2))
        for cursor, _diagnostic in self._diagnostic_ranges:
            if cursor.hasSelection():
                continue
            rect = self.cursorRect(cursor)
            if not rect.intersects(self.viewport().rect()):
                continue
            x, top, bottom = rect.left() + 1, rect.top() + 2, rect.bottom() - 2
            painter.drawLine(x, top, x, bottom)
            painter.drawLine(x - 2, top, x + 2, top)
            painter.drawLine(x - 2, bottom, x + 2, bottom)
