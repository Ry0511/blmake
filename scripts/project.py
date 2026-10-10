import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "src" / "blmake"


def tool(name: str) -> str:
    return shutil.which(name) or sys.exit(f"couldn't find {name} on PATH")


def build_dir() -> Path:
    def last_used(directory: Path) -> float:
        files = [directory / "compile_commands.json", directory / ".ninja_log"]
        return max(f.stat().st_mtime for f in files if f.is_file())

    dirs = [p.parent for p in (ROOT / "out" / "build").glob("*/compile_commands.json")]
    if not dirs:
        sys.exit("no compile_commands.json under out/build, configure a CMake preset first")
    return max(dirs, key=last_used)


def pack() -> Path:

    def installed(directory: Path) -> list[Path]:
        return [p for p in directory.rglob("*") if p.is_file() and p.suffix != ".zip"]

    dirs = [d for d in (ROOT / "out" / "install").glob("*") if installed(d)]
    if not dirs:
        sys.exit("nothing under out/install, run cmake --install first")
    install = max(dirs, key=lambda d: max(p.stat().st_mtime for p in installed(d)))

    cmake = (ROOT / "CMakeLists.txt").read_text()
    version = re.search(r"project\s*\([^)]*?\bVERSION\s+([\d.]+)", cmake, re.DOTALL)
    arch = re.search(r"x86|x64", install.name)
    if version is None or arch is None:
        sys.exit(f"couldn't get the version from CMakeLists.txt or the arch from {install.name}")

    archive = install / f"blmake-{version.group(1)}-{arch.group()}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in installed(install):
            zf.write(path, path.relative_to(install))
    return archive


class Args(argparse.Namespace):
    format: bool = False
    check: bool = False
    fix: bool = False
    pack: bool = False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _ = parser.add_argument("--format", action="store_true", help="run clang-format over the sources")
    _ = parser.add_argument("--check", action="store_true", help="run clang-tidy over the sources")
    _ = parser.add_argument("--fix", action="store_true", help="apply clang-tidy's fixes")
    _ = parser.add_argument("--pack", action="store_true", help="zip the installed build into a release archive")
    args = parser.parse_args(namespace=Args())
    if not (args.format or args.check or args.fix or args.pack):
        parser.print_help()
        return 0

    result = 0
    if args.check or args.fix:
        build = build_dir()
        print(f"clang-tidy using {build}", flush=True)
        command = [
            sys.executable,
            str(Path(__file__).with_name("run-clang-tidy.py")),
            "-p",
            str(build),
            "-clang-tidy-binary",
            tool("clang-tidy"),
            "-clang-apply-replacements-binary",
            tool("clang-apply-replacements"),
            "-quiet",
        ]
        if args.fix:
            command.append("-fix")
        if "x86" in build.name:
            command.append("-extra-arg-before=--target=i686-pc-windows-msvc")
        command.append(r".*[/\\]src[/\\]blmake[/\\].*")
        result |= subprocess.run(command, cwd=ROOT, check=False).returncode

    if args.format:
        files = sorted(str(p) for p in SOURCES.rglob("*") if p.suffix in (".h", ".cpp"))
        result |= subprocess.run([tool("clang-format"), "-i", *files], cwd=ROOT, check=False).returncode

    if args.pack:
        print(f"packed {pack()}")

    return result


if __name__ == "__main__":
    sys.exit(main())
