"""Selector de unrolling: preferencias, controles Qt y compilaciones reales.

Desde la raíz: QT_QPA_PLATFORM=offscreen PYTHONPATH=ide \
    ide/.venv/bin/python -m unittest discover -s ide/tests -v
"""

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import PropertyMock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QSettings
from PySide6.QtTest import QSignalSpy
from PySide6.QtWidgets import QApplication

from piper_ide.infrastructure.settings import load_optimizations, save_optimizations
from piper_ide.models.compilation import OptimizationOptions
from piper_ide.services.compiler import build_invocation
from piper_ide.ui.main_window import MainWindow


ROOT = Path(__file__).resolve().parents[2]
SOURCE = """routine B32 main[] {
    create B32 i.
    create B32 suma.
    suma store 0.
    loop [i & 0 -> 4 & 1] {
        suma store suma + i.
    }
    output suma.
}
"""


class UnrollOptionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.preferences = tempfile.TemporaryDirectory(prefix="piper-test-preferences-")
        cls.previous_format = QSettings.defaultFormat()
        QSettings.setDefaultFormat(QSettings.Format.IniFormat)
        QSettings.setPath(QSettings.Format.IniFormat, QSettings.Scope.UserScope, cls.preferences.name)

    @classmethod
    def tearDownClass(cls):
        QSettings.setDefaultFormat(cls.previous_format)
        cls.preferences.cleanup()

    def setUp(self):
        self.settings = QSettings(QSettings.defaultFormat(), QSettings.Scope.UserScope,
                                  "Piper", "Piper IDE")
        self.settings.clear()
        self.window = None

    def tearDown(self):
        if self.window is not None:
            self.window.close()
            self.window.deleteLater()
            self.app.processEvents()

    def create_window(self):
        self.window = MainWindow()
        return self.window

    def test_arguments_off_auto_and_manual(self):
        for enabled, mode, factor, expected in (
            (False, "auto", 2, "off"), (False, "manual", 4, "off"),
            (True, "auto", 4, "auto"), (True, "manual", 2, "2"),
            (True, "manual", 4, "4"), (True, "manual", 4096, "4096"),
        ):
            with self.subTest(enabled=enabled, mode=mode, factor=factor):
                options = OptimizationOptions(unroll=enabled, unroll_mode=mode, unroll_factor=factor)
                self.assertEqual(options.arguments[:2], ("--unroll", expected))

    def test_reject_invalid_configuration(self):
        for factor in (0, 1, 4097, 2.5, True):
            with self.subTest(factor=factor), self.assertRaises(ValueError):
                OptimizationOptions(unroll_factor=factor)
        with self.assertRaises(ValueError):
            OptimizationOptions(unroll_mode="unknown")

    def test_preferences_roundtrip_including_disabled_manual(self):
        for enabled, mode in ((True, "manual"), (False, "manual"), (True, "auto")):
            with self.subTest(enabled=enabled, mode=mode):
                expected = OptimizationOptions(unroll=enabled, unroll_mode=mode, unroll_factor=4,
                                               dce=True, rename=True, allow_spill=False)
                save_optimizations(expected)
                self.assertEqual(load_optimizations(), expected)

    def test_load_previous_automatic_preferences(self):
        self.settings.setValue("compilation/unroll", True)
        self.assertEqual(load_optimizations(), OptimizationOptions(unroll=True))

    def test_load_old_manual_preferences(self):
        self.settings.setValue("compilation/unroll", True)
        self.settings.setValue("compilation/useFactor", True)
        self.settings.setValue("compilation/factor", 4)
        self.assertEqual(load_optimizations(), OptimizationOptions(
            unroll=True, unroll_mode="manual", unroll_factor=4))

    def test_invalid_saved_factor_uses_defaults(self):
        self.settings.setValue("compilation/factor", 0)
        self.assertEqual(load_optimizations(), OptimizationOptions())

    def test_ui_enablement_and_preserved_selection(self):
        window = self.create_window()
        self.assertFalse(window.unroll_mode.isEnabled())
        self.assertFalse(window.unroll_factor.isEnabled())
        self.assertTrue(window.unroll_factor.isHidden())
        window.unroll_checkbox.setChecked(True)
        self.assertTrue(window.unroll_mode.isEnabled())
        self.assertFalse(window.unroll_factor.isEnabled())
        window.unroll_mode.setCurrentIndex(window.unroll_mode.findData("manual"))
        self.assertTrue(window.unroll_factor.isEnabled())
        self.assertFalse(window.unroll_factor.isHidden())
        window.unroll_factor.setValue(4)
        self.assertEqual(window._selected_optimizations().unroll_argument, "4")
        window.unroll_mode.setCurrentIndex(window.unroll_mode.findData("auto"))
        self.assertTrue(window.unroll_factor.isHidden())
        self.assertEqual(window._selected_optimizations().unroll_argument, "auto")
        window.unroll_mode.setCurrentIndex(window.unroll_mode.findData("manual"))
        self.assertFalse(window.unroll_factor.isHidden())
        self.assertEqual(window.unroll_factor.value(), 4)
        window.unroll_checkbox.setChecked(False)
        self.assertEqual(window._selected_optimizations().unroll_argument, "off")
        window.unroll_checkbox.setChecked(True)
        self.assertEqual(window._selected_optimizations().unroll_argument, "4")
        window.compile_target.setCurrentIndex(window.compile_target.findData("ins"))
        self.assertFalse(window.unroll_mode.isEnabled())
        self.assertFalse(window.unroll_factor.isEnabled())
        window.compile_target.setCurrentIndex(window.compile_target.findData("all"))
        self.assertTrue(window.unroll_factor.isEnabled())
        with patch.object(type(window.compiler), "running", new_callable=PropertyMock, return_value=True):
            window._update_optimization_controls()
            self.assertFalse(window.unroll_mode.isEnabled())
            self.assertFalse(window.unroll_factor.isEnabled())
        window._update_optimization_controls()
        self.assertTrue(window.unroll_factor.isEnabled())
        self.assertEqual(load_optimizations(), window._selected_optimizations())

    def test_ui_restores_mode_and_factor(self):
        save_optimizations(OptimizationOptions(unroll=True, unroll_mode="manual", unroll_factor=4))
        window = self.create_window()
        self.assertEqual(window.unroll_mode.currentData(), "manual")
        self.assertEqual(window.unroll_factor.value(), 4)
        self.assertTrue(window.unroll_factor.isEnabled())
        self.assertFalse(window.unroll_factor.isHidden())

    def test_inspection_omits_unrolling_flags(self):
        invocation = build_invocation(ROOT / "tests/archivadas/codeOPgen/loop.rwt", ROOT, "ins",
                                      OptimizationOptions(unroll=True, unroll_mode="manual", unroll_factor=4))
        self.assertNotIn("--unroll", invocation.arguments)

    @unittest.skipUnless((ROOT / "bin/pipoc").is_file(), "ejecutar make all primero")
    def test_ui_selection_compiles_through_service_and_exports_real_reports(self):
        window = self.create_window()
        window.unroll_checkbox.setChecked(True)
        with tempfile.TemporaryDirectory(prefix="piper-test-unroll-") as directory:
            folder = Path(directory)
            source = folder / "loop.rwt"
            source.write_text(SOURCE)
            results = []
            failures = []
            window.compiler.finished.connect(results.append)
            window.compiler.failed.connect(failures.append)
            for mode, factor, expected in (("auto", 2, 2), ("manual", 4, 4), ("manual", 8, None)):
                with self.subTest(mode=mode, factor=factor):
                    window.unroll_mode.setCurrentIndex(window.unroll_mode.findData(mode))
                    window.unroll_factor.setValue(factor)
                    options = window._selected_optimizations()
                    spy = QSignalSpy(window.compiler.finished)
                    self.assertTrue(window.compiler.compile(source, ROOT, "all", options))
                    self.assertTrue(spy.wait(15000), failures)
                    result = results[-1]
                    self.assertTrue(result.succeeded, result.stderr + str(result.artifact_error))
                    report_path = next(p for p in result.invocation.artifacts if p.name.endswith(".opt.json"))
                    report = json.loads(report_path.read_text())
                    self.assertEqual(report["options"]["unroll_mode"], mode)
                    self.assertEqual(report["options"]["unroll_factor"], factor)
                    details = report["passes"]["unrolling"]
                    if expected is None:
                        self.assertEqual(details["status"], "sin cambios")
                        self.assertEqual(details["loops"]["skipped"], 1)
                    else:
                        self.assertEqual(details["status"], "aplicada")
                        self.assertIn(f"factor={expected}", result.stderr)
                        self.assertEqual(details["loops"]["unrolled"], 1)
                    self.assertTrue(all(p.is_file() for p in result.invocation.artifacts))
            window.unroll_checkbox.setChecked(False)
            spy = QSignalSpy(window.compiler.finished)
            self.assertTrue(window.compiler.compile(source, ROOT, "all", window._selected_optimizations()))
            self.assertTrue(spy.wait(15000), failures)
            result = results[-1]
            self.assertTrue(result.succeeded, result.stderr)
            self.assertIn("unroll=off", result.stderr)
            self.assertEqual((result.invocation.output_dir / "loop.ir").read_text(),
                             (result.invocation.output_dir / "loop.unroll.ir").read_text())


if __name__ == "__main__":
    unittest.main()
