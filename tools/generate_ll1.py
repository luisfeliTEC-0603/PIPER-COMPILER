#!/usr/bin/env python3
"""Genera los datos C de una gramática LL(1) declarativa.

El generador valida los símbolos, calcula nullable/FIRST/FOLLOW y rechaza la
gramática si dos producciones intentan ocupar la misma celda predictiva.
"""

import argparse
import json
import re
import shlex
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Sequence, Set, Tuple


class GrammarError(Exception):
    """Error legible en la gramática de entrada."""


@dataclass(frozen=True)
class Symbol:
    name: str
    display: str


@dataclass(frozen=True)
class Production:
    lhs: str
    rhs: Tuple[str, ...]


@dataclass(frozen=True)
class Grammar:
    terminals: Tuple[Symbol, ...]
    nonterminals: Tuple[Symbol, ...]
    start: str
    productions: Tuple[Production, ...]


RULE_TOKEN = re.compile(r"%empty|[A-Z][A-Z0-9_]*|[:|;]")
SYMBOL_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")


def parse_directive(parts: Sequence[str], line: int,
                    terminals: List[Symbol],
                    nonterminals: List[Symbol]) -> Tuple[str, str]:
    directive = parts[0]
    if directive in ("%terminal", "%nonterminal"):
        if len(parts) != 3:
            raise GrammarError(
                "línea {}: {} requiere un identificador y un nombre legible".format(
                    line, directive
                )
            )
        name, display = parts[1], parts[2]
        if not SYMBOL_NAME.match(name):
            raise GrammarError(
                "línea {}: identificador inválido {!r}".format(line, name)
            )
        destination = terminals if directive == "%terminal" else nonterminals
        destination.append(Symbol(name, display))
        return "", ""
    if directive == "%start":
        if len(parts) != 2:
            raise GrammarError(
                "línea {}: %start requiere exactamente un no terminal".format(line)
            )
        return "start", parts[1]
    raise GrammarError(
        "línea {}: directiva desconocida {!r}".format(line, directive)
    )


def tokenize_rules(text: str) -> List[str]:
    tokens: List[str] = []
    position = 0
    for match in RULE_TOKEN.finditer(text):
        unexpected = text[position:match.start()]
        if unexpected.strip():
            raise GrammarError(
                "texto inesperado en las producciones: {!r}".format(
                    unexpected.strip().splitlines()[0]
                )
            )
        tokens.append(match.group(0))
        position = match.end()
    if text[position:].strip():
        raise GrammarError(
            "texto inesperado al final: {!r}".format(text[position:].strip())
        )
    return tokens


def parse_productions(tokens: Sequence[str]) -> List[Production]:
    productions: List[Production] = []
    index = 0

    while index < len(tokens):
        lhs = tokens[index]
        index += 1
        if lhs in ("%empty", ":", "|", ";"):
            raise GrammarError("se esperaba un no terminal y se encontró {!r}".format(lhs))
        if index >= len(tokens) or tokens[index] != ":":
            raise GrammarError("falta ':' después de {}".format(lhs))
        index += 1

        rhs: List[str] = []
        explicit_empty = False
        while True:
            if index >= len(tokens):
                raise GrammarError("falta ';' al terminar la regla {}".format(lhs))
            token = tokens[index]
            index += 1

            if token == "%empty":
                if rhs or explicit_empty:
                    raise GrammarError(
                        "%empty debe ser la única cosa en una alternativa de {}".format(lhs)
                    )
                explicit_empty = True
                continue

            if token in ("|", ";"):
                if not rhs and not explicit_empty:
                    raise GrammarError(
                        "alternativa vacía en {}; escriba %empty explícitamente".format(lhs)
                    )
                productions.append(Production(lhs, tuple(rhs)))
                rhs = []
                explicit_empty = False
                if token == ";":
                    break
                continue

            if explicit_empty:
                raise GrammarError(
                    "%empty debe ser la única cosa en una alternativa de {}".format(lhs)
                )
            if token == ":":
                raise GrammarError("':' inesperado dentro de la regla {}".format(lhs))
            rhs.append(token)

    return productions


def parse_grammar(path: Path) -> Grammar:
    terminals: List[Symbol] = []
    nonterminals: List[Symbol] = []
    start = ""
    rule_lines: List[str] = []
    in_rules = False

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        stripped = raw_line.strip()
        if not in_rules:
            if not stripped or stripped.startswith("#"):
                continue
            if stripped == "%%":
                in_rules = True
                continue
            try:
                parts = shlex.split(raw_line, comments=True, posix=True)
            except ValueError as error:
                raise GrammarError("línea {}: {}".format(line_number, error)) from error
            if not parts:
                continue
            kind, value = parse_directive(
                parts, line_number, terminals, nonterminals
            )
            if kind == "start":
                if start:
                    raise GrammarError("%start aparece más de una vez")
                start = value
            continue

        rule_lines.append(raw_line.partition("#")[0])

    if not in_rules:
        raise GrammarError("falta el separador %% antes de las producciones")
    if not terminals:
        raise GrammarError("la gramática no declara terminales")
    if not nonterminals:
        raise GrammarError("la gramática no declara no terminales")
    if not start:
        raise GrammarError("la gramática no declara %start")

    terminal_names = [symbol.name for symbol in terminals]
    nonterminal_names = [symbol.name for symbol in nonterminals]
    all_names = terminal_names + nonterminal_names
    duplicates = sorted({name for name in all_names if all_names.count(name) > 1})
    if duplicates:
        raise GrammarError(
            "símbolos declarados más de una vez: {}".format(", ".join(duplicates))
        )
    if "EOF" not in terminal_names:
        raise GrammarError("debe existir el terminal EOF")
    if start not in nonterminal_names:
        raise GrammarError("el símbolo inicial {} no es un no terminal".format(start))

    productions = parse_productions(tokenize_rules("\n".join(rule_lines)))
    known = set(all_names)
    nonterminal_set = set(nonterminal_names)
    defined = set()
    for production in productions:
        if production.lhs not in nonterminal_set:
            raise GrammarError(
                "{} aparece a la izquierda pero no es un no terminal".format(
                    production.lhs
                )
            )
        defined.add(production.lhs)
        for symbol in production.rhs:
            if symbol not in known:
                raise GrammarError(
                    "símbolo {} no declarado en la regla {}".format(
                        symbol, production.lhs
                    )
                )
    missing = [name for name in nonterminal_names if name not in defined]
    if missing:
        raise GrammarError(
            "no terminales sin producciones: {}".format(", ".join(missing))
        )

    return Grammar(
        tuple(terminals), tuple(nonterminals), start, tuple(productions)
    )


def sequence_first(
    sequence: Sequence[str],
    terminals: Set[str],
    nullable: Set[str],
    first: Dict[str, Set[str]],
) -> Tuple[Set[str], bool]:
    result: Set[str] = set()
    for symbol in sequence:
        if symbol in terminals:
            result.add(symbol)
            return result, False
        result.update(first[symbol])
        if symbol not in nullable:
            return result, False
    return result, True


def calculate_table(grammar: Grammar) -> Dict[Tuple[str, str], int]:
    terminals = {symbol.name for symbol in grammar.terminals}
    nonterminals = {symbol.name for symbol in grammar.nonterminals}
    nullable: Set[str] = set()

    changed = True
    while changed:
        changed = False
        for production in grammar.productions:
            if all(
                symbol in nonterminals and symbol in nullable
                for symbol in production.rhs
            ) and production.lhs not in nullable:
                nullable.add(production.lhs)
                changed = True

    first: Dict[str, Set[str]] = {name: set() for name in nonterminals}
    changed = True
    while changed:
        changed = False
        for production in grammar.productions:
            additions, _ = sequence_first(
                production.rhs, terminals, nullable, first
            )
            before = len(first[production.lhs])
            first[production.lhs].update(additions)
            changed = changed or len(first[production.lhs]) != before

    follow: Dict[str, Set[str]] = {name: set() for name in nonterminals}
    follow[grammar.start].add("EOF")
    changed = True
    while changed:
        changed = False
        for production in grammar.productions:
            for position, symbol in enumerate(production.rhs):
                if symbol not in nonterminals:
                    continue
                suffix_first, suffix_nullable = sequence_first(
                    production.rhs[position + 1 :], terminals, nullable, first
                )
                additions = set(suffix_first)
                if suffix_nullable:
                    additions.update(follow[production.lhs])
                before = len(follow[symbol])
                follow[symbol].update(additions)
                changed = changed or len(follow[symbol]) != before

    table: Dict[Tuple[str, str], int] = {}
    for production_index, production in enumerate(grammar.productions):
        lookaheads, production_nullable = sequence_first(
            production.rhs, terminals, nullable, first
        )
        if production_nullable:
            lookaheads.update(follow[production.lhs])
        if not lookaheads:
            raise GrammarError(
                "la producción {} no puede seleccionarse con ningún terminal".format(
                    format_production(production)
                )
            )
        for terminal in lookaheads:
            cell = (production.lhs, terminal)
            previous = table.get(cell)
            if previous is not None and previous != production_index:
                raise GrammarError(
                    "conflicto LL(1) en [{}, {}]: {} / {}".format(
                        production.lhs,
                        terminal,
                        format_production(grammar.productions[previous]),
                        format_production(production),
                    )
                )
            table[cell] = production_index
    return table


def format_production(production: Production) -> str:
    return "{} -> {}".format(
        production.lhs, " ".join(production.rhs) if production.rhs else "ε"
    )


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def c_symbol(symbol: str, terminals: Set[str]) -> str:
    if symbol in terminals:
        return "LL1_TERMINAL_SYMBOL(LL1_T_{})".format(symbol)
    return "LL1_NONTERMINAL_SYMBOL(LL1_N_{})".format(symbol)


def generated_banner(source: Path) -> List[str]:
    return [
        "/* Archivo generado por tools/generate_ll1.py.",
        " * Fuente: {}".format(source.as_posix()),
        " * No editar manualmente; cambie piper.ll1 y ejecute make.",
        " */",
        "",
    ]


def render_symbols(grammar: Grammar, source: Path) -> str:
    maximum_rhs = max(len(production.rhs) for production in grammar.productions)
    lines = generated_banner(source)
    lines.extend(
        [
            "#ifndef PIPER_LL1_GRAMMAR_SYMBOLS_GENERATED_H",
            "#define PIPER_LL1_GRAMMAR_SYMBOLS_GENERATED_H",
            "",
            "#define LL1_MAX_RHS {}".format(maximum_rhs),
            "",
            "typedef enum {",
        ]
    )
    lines.extend(
        "    LL1_T_{},".format(symbol.name) for symbol in grammar.terminals
    )
    lines.extend(["    LL1_T_COUNT", "} Ll1Terminal;", "", "typedef enum {"])
    lines.extend(
        "    LL1_N_{},".format(symbol.name) for symbol in grammar.nonterminals
    )
    lines.extend(
        [
            "    LL1_N_COUNT",
            "} Ll1Nonterminal;",
            "",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def render_data(
    grammar: Grammar, table: Dict[Tuple[str, str], int], source: Path
) -> str:
    terminal_names = [symbol.name for symbol in grammar.terminals]
    terminals = set(terminal_names)
    lines = generated_banner(source)
    lines.append("static const Ll1Production productions[] = {")
    for production in grammar.productions:
        rhs = [c_symbol(symbol, terminals) for symbol in production.rhs]
        initializer = ", ".join(rhs) if rhs else "0"
        lines.append(
            "    {LL1_N_%s, %d, {%s}},"
            % (production.lhs, len(production.rhs), initializer)
        )
    lines.extend(["};", "", "static const char *const terminal_names[LL1_T_COUNT] = {"])
    for symbol in grammar.terminals:
        lines.append(
            "    [LL1_T_{}] = {},".format(symbol.name, c_string(symbol.display))
        )
    lines.extend(
        [
            "};",
            "",
            "static const char *const nonterminal_names[LL1_N_COUNT] = {",
        ]
    )
    for symbol in grammar.nonterminals:
        lines.append(
            "    [LL1_N_{}] = {},".format(symbol.name, c_string(symbol.display))
        )
    lines.extend(
        [
            "};",
            "",
            "/* Cero significa celda vacía; los índices de producción usan +1. */",
            "static const uint16_t predictive_table[LL1_N_COUNT][LL1_T_COUNT] = {",
        ]
    )
    for nonterminal in grammar.nonterminals:
        entries = [
            terminal
            for terminal in grammar.terminals
            if (nonterminal.name, terminal.name) in table
        ]
        if not entries:
            continue
        lines.append("    [LL1_N_{}] = {{".format(nonterminal.name))
        for terminal in entries:
            encoded = table[(nonterminal.name, terminal.name)] + 1
            lines.append(
                "        [LL1_T_{}] = {},".format(terminal.name, encoded)
            )
        lines.append("    },")
    lines.extend(["};", ""])
    return "\n".join(lines)


def write_output(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Genera enums o datos C para la gramática predictiva"
    )
    parser.add_argument("grammar", type=Path, help="archivo .ll1 de entrada")
    parser.add_argument(
        "--kind", required=True, choices=("symbols", "data"), help="salida a generar"
    )
    parser.add_argument("--output", required=True, type=Path, help="archivo de salida")
    arguments = parser.parse_args()

    try:
        grammar = parse_grammar(arguments.grammar)
        table = calculate_table(grammar)
        if len(grammar.productions) > 65535:
            raise GrammarError("hay demasiadas producciones para la tabla de 16 bits")
        if arguments.kind == "symbols":
            content = render_symbols(grammar, arguments.grammar)
        else:
            content = render_data(grammar, table, arguments.grammar)
        write_output(arguments.output, content)
    except (GrammarError, OSError) as error:
        print("generate_ll1: error: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
