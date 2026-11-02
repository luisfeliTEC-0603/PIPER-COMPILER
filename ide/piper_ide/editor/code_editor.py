"""Edición básica con números de línea y guías siempre visibles.

El resaltado, la asistencia y los diagnósticos se incorporarán después.
"""

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QColor, QPainter, QTextFormat
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
    guías en la indentación. Todavía no analiza la sintaxis de Piper.
    """

    def __init__(self, parent=None):
        """Configura texto sin ajuste de línea y sincroniza el margen con Qt."""
        super().__init__(parent)
        self.setObjectName("codeEditor")
        self.setAccessibleName("Código Piper de ejemplo")
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.line_numbers = _LineNumbers(self)
        self._colors = {}
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
        self.setExtraSelections([selection])

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
