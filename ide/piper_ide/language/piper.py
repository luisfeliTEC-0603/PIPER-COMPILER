"""Vocabulario visual tomado de src/lexer.l; no valida ni compila Piper."""

KEYWORDS = frozenset("create routine loop alwhen when fallback store bring from break output and or xor".split())
TYPES = frozenset("bool B8 uB8 B16 uB16 B32 uB32".split())
CONSTANTS = frozenset(("true", "false", "none"))
