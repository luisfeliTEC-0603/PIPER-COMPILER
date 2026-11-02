"""Edición, indentación, navegación y búsqueda sobre el documento Qt activo.

No analiza errores ni corrige terminadores. Los colores de sintaxis pertenecen
al resaltador de cada buffer; este widget dibuja números, guías y coincidencias.
"""

import re

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QColor, QPainter, QTextCursor, QTextDocument, QTextFormat
from PySide6.QtWidgets import QPlainTextEdit, QTextEdit, QWidget

from ..ui.theme import monospace_font


class _LineNumbers(QWidget):
    """Margen hijo del editor: delega el dibujo para compartir sus coordenadas."""

    def paintEvent(self, event):
        """Solicita al editor los números de los bloques actualmente visibles."""
        self.parent().paint_line_numbers(event)


class CodeEditor(QPlainTextEdit):
    """Editor de texto con decoración propia y edición estándar de Qt.

    QPlainTextEdit administra selección, cursor, desplazamiento y Deshacer.
    Este componente añade un margen independiente para los números y dibuja
    guías en la indentación. Tab/Shift+Tab y Enter son operaciones de edición,
    agrupadas para poder deshacerse, no correcciones automáticas del lenguaje.
    """

    def __init__(self, parent=None):
        """Configura texto sin ajuste de línea y sincroniza el margen con Qt."""
        super().__init__(parent)
        self.setObjectName("codeEditor")
        self.setAccessibleName("Editor de código")
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.line_numbers = _LineNumbers(self)
        self._colors = {}
        self._search_matches = []
        self.set_code_font_size(14)
        self.blockCountChanged.connect(self._update_margin)
        self.updateRequest.connect(self._update_numbers)
        self.cursorPositionChanged.connect(self._highlight_current_line)
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
        """Reserva espacio para los números tras cambiar la fuente o las líneas.

        El argumento opcional absorbe la cantidad emitida por blockCountChanged;
        el cálculo consulta directamente el estado actual del documento.
        """
        self.setViewportMargins(self._margin_width(), 0, 0, 0)
        self._position_numbers()

    def _position_numbers(self):
        """Alinea el widget del margen con el área interior del editor."""
        rect = self.contentsRect()
        self.line_numbers.setGeometry(QRect(rect.left(), rect.top(), self._margin_width(), rect.height()))

    def _update_numbers(self, rect, dy):
        """Acompaña el desplazamiento vertical y repinta solo el área afectada.

        Qt entrega un desplazamiento dy al hacer scroll y un rectángulo cuando
        cambia el contenido. Reutilizar esos datos evita repintar todo el margen.
        """
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
        """Dibuja el fondo de la línea activa mediante una selección adicional.

        ExtraSelection es una decoración: no modifica ni selecciona el texto
        real del usuario. Hasta recibir el primer tema no se aplica ningún color.
        """
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
        self.setExtraSelections(selections)

    def setDocument(self, document):
        """Cambia de buffer y elimina decoraciones que pertenecían al anterior."""
        self._search_matches = []
        super().setDocument(document)
        self._update_margin()
        self._highlight_current_line()

    def search_flags(self, case_sensitive=False, whole_words=False):
        """Construye las opciones nativas de búsqueda literal, sin expresiones regulares."""
        flags = QTextDocument.FindFlag(0)
        if case_sensitive:
            flags |= QTextDocument.FindFlag.FindCaseSensitively
        if whole_words:
            flags |= QTextDocument.FindFlag.FindWholeWords
        return flags

    def highlight_search(self, query: str, flags) -> tuple[int, bool]:
        """Destaca hasta mil coincidencias y devuelve (cantidad, hay_más).

        El límite evita crear millones de selecciones al buscar un carácter en
        un archivo grande. Siguiente/Anterior sigue buscando en todo el documento.
        Qt calcula las posiciones, incluyendo correctamente caracteres Unicode.
        """
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

    def keyPressEvent(self, event):
        """Mantiene la edición directa y añade indentación, sin modos ni cierre de pares."""
        if self.isReadOnly() or event.modifiers() & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier):
            super().keyPressEvent(event)
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
        super().keyPressEvent(event)

    def _visible_blocks(self, bottom):
        """Produce (bloque, y superior, altura) hasta el límite inferior en píxeles.

        Las posiciones del documento se traducen con contentOffset para obtener
        coordenadas de la zona visible, incluyendo líneas parcialmente cortadas.
        Números y guías usan el mismo recorrido para mantenerse alineados.
        """
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
        painter.fillRect(event.rect(), QColor(self._colors["editor"]))
        painter.setFont(self.font())
        painter.setPen(QColor(self._colors["muted"]))
        for block, top, height in self._visible_blocks(event.rect().bottom()):
            painter.drawText(0, top, self.line_numbers.width() - 12, height,
                             Qt.AlignmentFlag.AlignRight, str(block.blockNumber() + 1))

    def paintEvent(self, event):
        """Deja a Qt dibujar el texto y superpone guías solo en el espacio inicial.

        Las tabulaciones se interpretan como grupos de cuatro columnas para
        calcular las guías; no se cambia la indentación escrita por el usuario.
        """
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
