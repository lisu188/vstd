"""Install, relocate, and compile an independent public-target-only consumer."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run(*arguments):
    print("+", " ".join(str(value) for value in arguments), flush=True)
    subprocess.run([str(value) for value in arguments], check=True, timeout=180)


def cache_values(build):
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


@contextmanager
def consumer_workspace(build):
    directory = tempfile.TemporaryDirectory(prefix="package-consumer-", dir=build)
    workspace = Path(directory.name).resolve()
    if workspace.parent != build:
        raise RuntimeError("Generated workspace escaped its build directory")
    try:
        yield workspace
    finally:
        # Windows tooling can briefly hold an executable directory after CTest.
        # Retry only sharing/access violations; persistent cleanup errors still fail.
        for attempt in range(6):
            try:
                directory.cleanup()
                break
            except PermissionError as error:
                if os.name != "nt" or error.winerror not in (5, 32) or attempt == 5:
                    raise
                print("Waiting for Windows to release the consumer workspace", flush=True)
                time.sleep(0.1 * 2 ** attempt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--config", required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    cache = cache_values(build)
    with consumer_workspace(build) as workspace:
        installed = workspace / "installed"
        relocated = workspace / "relocated"
        run("cmake", "--install", build, "--prefix", installed, "--config", args.config)
        if not installed.resolve().is_relative_to(workspace) or not relocated.resolve().is_relative_to(workspace):
            raise RuntimeError("Generated installation escaped its temporary workspace")
        installed.rename(relocated)
        for package_file in relocated.rglob("*.cmake"):
            content = package_file.read_text(encoding="utf-8").replace("\\", "/")
            for original in (root, build, installed):
                if original.as_posix() in content:
                    raise RuntimeError("Installed package embeds an original source/build/prefix path")
        for name, package in (("installed", relocated), ("build-tree", build)):
            consumer_build = workspace / (name + "-consumer")
            arguments = ["cmake", "-S", root / "tests/package_consumer", "-B", consumer_build,
                         "-G", cache["CMAKE_GENERATOR"],
                         "-DCMAKE_BUILD_TYPE=" + args.config,
                         "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                         "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"]
            if name == "installed":
                arguments.append("-DCMAKE_PREFIX_PATH=" + str(package))
            else:
                arguments.append("-Dvstd_DIR=" + str(package))
            for key in ("CMAKE_CXX_COMPILER", "CMAKE_TOOLCHAIN_FILE", "VCPKG_TARGET_TRIPLET"):
                if cache.get(key):
                    arguments.append("-D" + key + "=" + cache[key])
            run(*arguments)
            found = Path(cache_values(consumer_build)["vstd_DIR"]).resolve()
            if not found.is_relative_to(package.resolve()):
                raise RuntimeError("Consumer found an unrelated vstd package")
            run("cmake", "--build", consumer_build, "--config", args.config, "--parallel", "2")
            run("ctest", "--test-dir", consumer_build, "-C", args.config, "--output-on-failure")
    print("Installed relocation and build-tree public-target consumers passed")


if __name__ == "__main__":
    main()
