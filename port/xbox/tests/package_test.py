"""tools/xbox_package.py's devkit memory flag: the XBE header's init flags
(offset 0x124) get XINIT_LIMIT_DEVKIT_MEMORY (0x4) set or cleared, and nothing
else changes. And --map-pack: a pack's maps and maplist.txt in maps_<id>.

    python3 port/xbox/tests/package_test.py
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import xbox_package  # noqa: E402


def image(flags: int) -> bytes:
    data = bytearray(b"XBEH" + bytes(0x200))
    data[0x124:0x128] = flags.to_bytes(4, "little")
    data[0x300 - 4:0x300] = b"tail"
    return bytes(data)


class DevkitMemoryLimit(unittest.TestCase):
    def flags(self, data: bytes) -> int:
        return int.from_bytes(data[0x124:0x128], "little")

    def test_set_keeps_the_other_flags(self):
        patched = xbox_package.set_devkit_memory_limit(image(0x1), True)
        self.assertEqual(self.flags(patched), 0x5)

    def test_clear(self):
        patched = xbox_package.set_devkit_memory_limit(image(0x5), False)
        self.assertEqual(self.flags(patched), 0x1)

    def test_only_the_flags_change(self):
        before = image(0x1)
        after = xbox_package.set_devkit_memory_limit(before, True)
        self.assertEqual(len(before), len(after))
        self.assertEqual(before[:0x124], after[:0x124])
        self.assertEqual(before[0x128:], after[0x128:])

    def test_not_an_xbe(self):
        with self.assertRaises(ValueError):
            xbox_package.set_devkit_memory_limit(b"MZ" + bytes(0x200), True)
        with self.assertRaises(ValueError):
            xbox_package.set_devkit_memory_limit(b"XBEH", True)


class MapPack(unittest.TestCase):
    def run_package(self, root: Path, *extra: str) -> subprocess.CompletedProcess:
        script = Path(__file__).resolve().parents[3] / "tools" / "xbox_package.py"
        return subprocess.run([sys.executable, str(script), "--xbe", str(root / "default.xbe"), "--maps",
                               str(root / "maps"), "--out", str(root / "out"), *extra], capture_output=True, text=True)

    def test_pack_in_its_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "default.xbe").write_bytes(image(0x1))
            (root / "maps").mkdir()
            (root / "maps" / "ui.map").write_bytes(b"retail ui")
            (root / "maps" / "bloodgulch.map").write_bytes(b"retail")
            (root / "pack").mkdir()
            (root / "pack" / "bloodgulch.map").write_bytes(b"pack")
            (root / "pack" / "maplist.txt").write_bytes(b"bloodgulch - Abyss")
            (root / "pack" / "readme.txt").write_bytes(b"left out")
            result = self.run_package(root, "--map-pack", f"cex={root / 'pack'}")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "out" / "maps" / "bloodgulch.map").read_bytes(), b"retail")
            self.assertEqual((root / "out" / "maps_cex" / "bloodgulch.map").read_bytes(), b"pack")
            self.assertTrue((root / "out" / "maps_cex" / "maplist.txt").is_file())
            self.assertFalse((root / "out" / "maps_cex" / "readme.txt").exists())
            for bad in ("ce=" + str(root / "pack"), "CEX=" + str(root / "pack"), "cex=" + str(root / "none"), "cex"):
                self.assertNotEqual(self.run_package(root, "--map-pack", bad).returncode, 0, bad)


if __name__ == "__main__":
    unittest.main()
