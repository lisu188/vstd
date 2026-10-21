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


class OwnedCallbackProbeCacheTest(unittest.TestCase):
    def testFailedStandardCallbackProbeDoesNotBlockOwnedCallbackProbe(self):
        cmake = shutil.which("cmake")
        compiler = shutil.which("clang-cl" if os.name == "nt" else "c++")
        if cmake is None or compiler is None or shutil.which("ninja") is None:
            self.skipTest("CMake, Ninja and a C++23 compiler are required for callback probe cache regression")
        root = Path(__file__).resolve().parents[1]
        source = (root / "cmake/RequireCxx26.cmake").read_text(encoding="utf-8")
        helper_start = source.index("function(vstd_require_cxx26_feature")
        helper_end = source.index("endfunction()", helper_start) + len("endfunction()")
        callback_probe = source[source.index("include(CMakePushCheckState)") :]
        callback_probe = callback_probe.replace("${CMAKE_CURRENT_LIST_DIR}/..", root.as_posix())
        with tempfile.TemporaryDirectory(prefix="vstd-callback-cache-") as temporary:
            directory = Path(temporary)
            (directory / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(callback_cache LANGUAGES CXX)\n"
                "set(CMAKE_CXX_STANDARD 23)\n"
                "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
                "include(CheckCXXSourceCompiles)\n" + source[helper_start:helper_end] + "\n" + callback_probe,
                encoding="utf-8",
            )
            build = directory / "build"
            result = subprocess.run(
                [
                    cmake,
                    "-S",
                    str(directory),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_CXX_COMPILER={compiler}",
                    "-DVSTD_CXX26_MOVE_ONLY_FUNCTION:INTERNAL=FALSE",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            self.assertRegex(cache, r"(?m)^VSTD_CXX26_MOVE_ONLY_FUNCTION:INTERNAL=FALSE$")
            self.assertRegex(cache, r"(?m)^VSTD_CXX26_OWNED_MOVE_ONLY_FUNCTION:INTERNAL=1$")


if __name__ == "__main__":
    unittest.main()
