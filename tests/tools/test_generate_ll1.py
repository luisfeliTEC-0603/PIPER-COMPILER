"""Casos de clase calculados a mano y correspondencia del reporte con la matriz."""

import importlib.util
from html.parser import HTMLParser
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("piper_ll1_generator", ROOT / "tools/generate_ll1.py")
generator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generator
SPEC.loader.exec_module(generator)


def grammar(terminals, nonterminals, rules):
    return generator.Grammar(
        tuple(generator.Symbol(name, display) for name, display in terminals),
        tuple(generator.Symbol(name, name) for name in nonterminals),
        nonterminals[0],
        tuple(generator.Production(lhs, tuple(rhs)) for lhs, rhs in rules),
    )


class MatrixReader(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_matrix = False
        self.nonterminal = None
        self.terminal = None
        self.cells = {}
        self.production_ids = {}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "table":
            self.in_matrix = attrs.get("class") == "matrix"
        if not self.in_matrix:
            return
        if tag == "tr":
            self.nonterminal = attrs.get("data-nonterminal")
        if tag == "td":
            self.terminal = attrs["data-terminal"]
            key = (self.nonterminal, self.terminal)
            self.cells[key] = ""
            if "data-production" in attrs:
                self.production_ids[key] = int(attrs["data-production"])

    def handle_data(self, data):
        if self.in_matrix and self.terminal is not None:
            self.cells[(self.nonterminal, self.terminal)] += data

    def handle_endtag(self, tag):
        if tag == "td":
            self.terminal = None
        if tag == "table":
            self.in_matrix = False


class GeneratorTests(unittest.TestCase):
    def test_nullable_sequence_and_follow_propagation(self):
        g = grammar([("EOF", "$"), ("A_TOKEN", "a"), ("B_TOKEN", "b")],
                    ["S", "A", "B"],
                    [("S", ["A", "B"]), ("A", ["A_TOKEN"]), ("A", []),
                     ("B", ["B_TOKEN"]), ("B", [])])
        a = generator.analyze_grammar(g)
        self.assertEqual(a.nullable, {"S", "A", "B"})
        self.assertEqual(a.first, {"S": {"A_TOKEN", "B_TOKEN"},
                                   "A": {"A_TOKEN"}, "B": {"B_TOKEN"}})
        self.assertEqual(a.follow, {"S": {"EOF"}, "A": {"B_TOKEN", "EOF"},
                                    "B": {"EOF"}})
        self.assertEqual(a.table, {("S", "A_TOKEN"): 0, ("S", "B_TOKEN"): 0,
                                   ("S", "EOF"): 0, ("A", "A_TOKEN"): 1,
                                   ("A", "B_TOKEN"): 2, ("A", "EOF"): 2,
                                   ("B", "B_TOKEN"): 3, ("B", "EOF"): 4})

    def test_first_needs_multiple_passes_and_stops_at_nonnullable(self):
        g = grammar([("EOF", "$"), ("X", "x"), ("Y", "y")],
                    ["S", "A", "B"],
                    [("S", ["A", "Y"]), ("A", ["B"]), ("B", ["X"])])
        a = generator.analyze_grammar(g)
        self.assertEqual(a.first, {"S": {"X"}, "A": {"X"}, "B": {"X"}})
        self.assertEqual(a.follow, {"S": {"EOF"}, "A": {"Y"}, "B": {"Y"}})
        self.assertEqual(a.nullable, set())

    def test_textbook_expression_table(self):
        g = grammar(
            [("EOF", "$"), ("ID", "id"), ("EQ", "=="), ("LT", "<"),
             ("PLUS", "+"), ("POW", "^"), ("LP", "("), ("CLOSE", ")")],
            ["E", "EP", "R", "RP", "T", "TP", "F", "FP", "P"],
            [("E", ["R", "EP"]), ("EP", ["EQ", "R", "EP"]), ("EP", []),
             ("R", ["T", "RP"]), ("RP", ["LT", "T", "RP"]), ("RP", []),
             ("T", ["F", "TP"]), ("TP", ["PLUS", "F", "TP"]), ("TP", []),
             ("F", ["P", "FP"]), ("FP", ["POW", "P", "FP"]), ("FP", []),
             ("P", ["ID"]), ("P", ["LP", "E", "CLOSE"])])
        a = generator.analyze_grammar(g)
        expected = {}
        for n, index in [("E", 0), ("R", 3), ("T", 6), ("F", 9)]:
            for t in ("ID", "LP"):
                expected[n, t] = index
        for n, token, index, follow in [
            ("EP", "EQ", 1, ["CLOSE", "EOF"]),
            ("RP", "LT", 4, ["EQ", "CLOSE", "EOF"]),
            ("TP", "PLUS", 7, ["EQ", "LT", "CLOSE", "EOF"]),
            ("FP", "POW", 10, ["EQ", "LT", "PLUS", "CLOSE", "EOF"]),
        ]:
            expected[n, token] = index
            for t in follow:
                expected[n, t] = index + 1
        expected["P", "ID"] = 12
        expected["P", "LP"] = 13
        self.assertEqual(a.table, expected)

    def test_first_first_conflict(self):
        g = grammar([("EOF", "$"), ("A", "a"), ("B", "b")], ["S"],
                    [("S", ["A"]), ("S", ["A", "B"])])
        with self.assertRaisesRegex(generator.GrammarError, r"conflicto LL\(1\) en \[S, A\]"):
            generator.analyze_grammar(g)

    def test_first_follow_conflict(self):
        g = grammar([("EOF", "$"), ("TOKEN", "a")], ["S", "A"],
                    [("S", ["A", "TOKEN"]), ("A", ["TOKEN"]), ("A", [])])
        with self.assertRaisesRegex(generator.GrammarError, r"conflicto LL\(1\) en \[A, TOKEN\]"):
            generator.analyze_grammar(g)

    def test_piper_html_matches_every_predictive_cell(self):
        g = generator.parse_grammar(ROOT / "src/analysis/ll1/piper.ll1")
        a = generator.analyze_grammar(g)
        document = generator.render_html(g, a, Path("piper.ll1"))
        reader = MatrixReader()
        reader.feed(document)
        self.assertEqual(len(reader.cells), len(g.terminals) * len(g.nonterminals))
        displays = {s.name: s.display for s in (*g.terminals, *g.nonterminals)}
        for key, text in reader.cells.items():
            index = a.table.get(key)
            if index is None:
                self.assertEqual(text, "")
                self.assertNotIn(key, reader.production_ids)
            else:
                rhs = g.productions[index].rhs
                self.assertEqual(text, " ".join(displays[s] for s in rhs) or "ε")
                self.assertEqual(reader.production_ids[key], index + 1)
        self.assertNotIn("@@", document)
        self.assertIn("&lt;", document)
        self.assertEqual(generator.calculate_table(g), a.table)


if __name__ == "__main__":
    unittest.main()
