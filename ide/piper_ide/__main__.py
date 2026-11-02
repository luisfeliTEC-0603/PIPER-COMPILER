"""Entrada para ``python -m piper_ide``.

También admite ejecutar este archivo directamente desde un editor, delegando
en app.py para resolver los imports del paquete con el mismo intérprete.
"""

if __package__:
    from .app import main
else:
    from app import _run_from_source as main

if __name__ == "__main__":
    raise SystemExit(main())
