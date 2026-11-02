"""Datos temporales de la etapa visual, sin leer archivos ni ejecutar pipoc.

El ejemplo Piper omite intencionalmente el punto en ``i store i + 1`` para
acompañar el diagnóstico fijo de Problemas. Los fragmentos CLONE_LEFT/RIGHT
ilustran un clon Tipo II por cambio de identificadores. No son resultados
calculados por un analizador; se sustituirán al conectar los servicios reales.
"""

PIPER_SAMPLE = """~~ Factorial con variables locales y un ciclo.

routine B32 factorial[B32 n] {
    create B32 resultado.
    resultado store 1.
    create B32 i.
    i store 1.
    alwhen [i <= n] {
        resultado store resultado * i.
        i store i + 1
    }
    output resultado.
}

routine B32 main[] {
    create B32 r.
    r store factorial[5].
    output r.
}
"""

CONSOLE_SAMPLE = """[INFO] Vista de ejemplo · factorial.rwt.
[DEBUG] Aquí aparecerán stdout, stderr y mensajes de las herramientas.
[INFO] Etapa visual: el compilador aún no está conectado.
"""

CLONE_LEFT = """i store 1.
alwhen [i <= n] {
    resultado store resultado * i.
    i store i + 1.
}"""

CLONE_RIGHT = """indice store 1.
alwhen [indice <= limite] {
    acumulado store acumulado * indice.
    indice store indice + 1.
}"""
