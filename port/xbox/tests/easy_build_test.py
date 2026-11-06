"""tools/easy_build.py's helpers: answers, maps checks, command lines, file names.

    python3 port/xbox/tests/easy_build_test.py
"""
import datetime
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import easy_build  # noqa: E402


class Answers(unittest.TestCase):
    def test_players(self):
        self.assertEqual(easy_build.parse_players(" 32 "), 32)
        self.assertIsNone(easy_build.parse_players("20"))
        self.assertIsNone(easy_build.parse_players("many"))

    def test_yes_no(self):
        self.assertTrue(easy_build.parse_yes_no("Yes", False))
        self.assertFalse(easy_build.parse_yes_no("n", True))
        self.assertTrue(easy_build.parse_yes_no("", True))

    def test_clean_path(self):
        self.assertEqual(easy_build.clean_path('  "/a/b"  '), "/a/b")

    def test_out_name(self):
        self.assertEqual(easy_build.default_out_name(datetime.date(2026, 10, 18), "abc1234"),
                         "warthog-xbox-20261018-abc1234")
        self.assertEqual(easy_build.default_out_name(datetime.date(2026, 10, 18), ""), "warthog-xbox-20261018")


class Commands(unittest.TestCase):
    def test_configure_defaults(self):
        self.assertEqual(easy_build.configure_args("d.lib", "ox", 32, False, False),
                         ["chupathingyce/configure.py", "--console-port", "port/xbox", "--xbox-d3d8", "d.lib",
                          "--oxdk", "ox", "--console-players", "32", "--console-no-debug-asserts"])

    def test_configure_asserts(self):
        self.assertNotIn("--console-no-debug-asserts", easy_build.configure_args("d.lib", "ox", 32, False, False, True))

    def test_configure_options(self):
        args = easy_build.configure_args("d.lib", "ox", 32, True, True)
        self.assertIn("--release", args)
        self.assertIn("--console-debug-tools", args)
        self.assertEqual(args[args.index("--console-players") + 1], "32")

    def test_package(self):
        self.assertIn("--devkit-64mb", easy_build.package_args("m", "o", True))
        self.assertNotIn("--devkit-64mb", easy_build.package_args("m", "o", False))


class Maps(unittest.TestCase):
    def test_missing_ui_map(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(easy_build.check_maps(Path(folder))[0])

    def test_wrong_size_warns(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "ui.map").write_bytes(b"x" * 100)
            usable, notes = easy_build.check_maps(Path(folder))
            self.assertTrue(usable)
            self.assertTrue(any("14,145,536" in note for note in notes))


class Progress(unittest.TestCase):
    def test_fraction(self):
        self.assertEqual(easy_build.progress_fraction("[12/340] XBOX CC x"), (12, 340))
        self.assertIsNone(easy_build.progress_fraction("warning"))

    def test_hint(self):
        self.assertIn("library", easy_build.hint_for_failure("lld-link: error: cannot open file xnet.lib"))


if __name__ == "__main__":
    unittest.main()
