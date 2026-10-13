"""Check that environment setup never rewrites the checked-in toolchain baseline."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SetupSourcePreservationTest(unittest.TestCase):
    def testSetupPreservesSourceAndStandard(self):
        root = Path(__file__).resolve().parents[1]
        bash = os.environ.get("BASH") or shutil.which("bash")
        self.assertIsNotNone(bash, "Bash is required to exercise configure.sh")
        sources = ("configure.sh", "CMakeLists.txt", "vfunctional.h")
        with tempfile.TemporaryDirectory(prefix="vstd-setup-") as temporary:
            directory = Path(temporary)
            originals = {}
            for name in sources:
                originals[name] = (root / name).read_bytes()
                (directory / name).write_bytes(originals[name])
            environment = dict(os.environ, VSTD_SKIP_INSTALL="1", VSTD_SKIP_BUILD="1")
            result = subprocess.run(
                [bash, "configure.sh"],
                cwd=directory,
                env=environment,
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name, original in originals.items():
                self.assertEqual((directory / name).read_bytes(), original, name)
            self.assertEqual({entry.name for entry in directory.iterdir()}, set(sources))


if __name__ == "__main__":
    unittest.main()
