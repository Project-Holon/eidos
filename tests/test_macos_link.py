"""Exercise optional Mach-O symbols with the actual host compiler/linker."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import macos_link


class Symbols(unittest.TestCase):
    def test_nm(self):
        weak, required = macos_link.weak_symbols("""
                 (undefined) weak external _optional
                 (undefined) external _required
0000000000000000 (__TEXT,__text) weak external _defined
""")
        self.assertEqual(weak, {"_optional"})
        self.assertEqual(required, {"_required"})

    @unittest.skipUnless(sys.platform == "darwin", "requires Apple ld")
    def test_native_link(self):
        wrapper = Path(macos_link.__file__)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.c"
            source.write_text("extern int optional(void) __attribute__((weak));\n"
                              "int main(void) { return optional ? optional() : 0; }\n")
            obj = root / "main.o"
            subprocess.run(["cc", "-c", str(source), "-o", str(obj)], check=True)
            exe = root / "probe"
            command = [sys.executable, str(wrapper), "--nm", "/usr/bin/nm", "--",
                       "cc", str(obj), "-o", str(exe)]
            subprocess.run(command, check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
            provider = root / "provider.c"
            provider.write_text("int optional(void) { return 7; }\n")
            subprocess.run(command + [str(provider)], check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 7)
            dylib = root / "libprovider.dylib"
            subprocess.run(["cc", "-dynamiclib", str(provider), "-o", str(dylib)], check=True)
            subprocess.run(command + [str(dylib)], check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 7)
            unused = root / "unused.c"
            unused.write_text("extern int optional(void);\n"
                              "int unused(void) { return optional(); }\n")
            unused_obj = root / "unused.o"
            archive = root / "libunused.a"
            subprocess.run(["cc", "-c", str(unused), "-o", str(unused_obj)], check=True)
            subprocess.run(["ar", "rcs", str(archive), str(unused_obj)], check=True)
            subprocess.run(command + [str(archive)], check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0,
                             "an unused archive member must not require its provider")
            source.write_text("extern int required(void);\n"
                              "int main(void) { return required(); }\n")
            subprocess.run(["cc", "-c", str(source), "-o", str(obj)], check=True)
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("_required", failed.stderr)
            weak = root / "weak.c"
            weak.write_text("extern int required(void) __attribute__((weak));\n"
                            "int another(void) { return required ? required() : 0; }\n")
            weak_obj = root / "weak.o"
            subprocess.run(["cc", "-c", str(weak), "-o", str(weak_obj)], check=True)
            failed = subprocess.run(command + [str(weak_obj)], capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0,
                                "a weak reference must not hide a required provider")
            self.assertIn("_required", failed.stderr)
            self.assertFalse(exe.exists(), "do not leave an executable with a missing provider")

    @unittest.skipUnless(sys.platform == "darwin", "requires Apple ld")
    def test_weak_only_member_left_out(self):
        """Apple ld loads an archive member for a weak reference; an ELF
        linker does not.  A member wanted only weakly, whose own references
        the program lacks (or whose definitions it has), is left out."""
        wrapper = Path(macos_link.__file__)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def cc(name, text):
                (root / (name + ".c")).write_text(text)
                subprocess.run(["cc", "-c", str(root / (name + ".c")), "-o", str(root / (name + ".o"))],
                               check=True)
                return str(root / (name + ".o"))

            main = cc("main", "extern int optional(void) __attribute__((weak));\n"
                              "int mine(void) { return 3; }\n"
                              "int main(void) { return optional ? optional() : mine(); }\n")
            # the member: the weak provider, a call the program has no
            # provider for, and a double of the program's own function
            wanted = cc("wanted", "extern int nowhere(void);\n"
                                  "int mine(void) { return 9; }\n"
                                  "int optional(void) { return nowhere(); }\n")
            # a second member of the same name in the same archive (sdk has
            # two fs.c): members are left out by position
            (root / "two").mkdir()
            twin_src = root / "two" / "wanted.c"
            twin_src.write_text("int unrelated(void) { return 1; }\n")
            twin = root / "two" / "wanted.o"
            subprocess.run(["cc", "-c", str(twin_src), "-o", str(twin)], check=True)
            archive = root / "liboptional.a"
            subprocess.run(["ar", "rcs", str(archive), wanted, str(twin)], check=True)
            exe = root / "probe"
            subprocess.run([sys.executable, str(wrapper), "--nm", "/usr/bin/nm", "--",
                            "cc", main, str(archive), "-o", str(exe)], check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 3,
                             "the weak-only member must be left out")


if __name__ == "__main__":
    unittest.main()
