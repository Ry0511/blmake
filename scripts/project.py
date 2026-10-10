import argparse
import shutil
import subprocess
import sys
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


class Args(argparse.Namespace):
    format: bool = False
    check: bool = False
    fix: bool = False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _ = parser.add_argument("--format", action="store_true", help="run clang-format over the sources")
    _ = parser.add_argument("--check", action="store_true", help="run clang-tidy over the sources")
    _ = parser.add_argument("--fix", action="store_true", help="apply clang-tidy's fixes")
    args = parser.parse_args(namespace=Args())
    if not (args.format or args.check or args.fix):
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

    return result


if __name__ == "__main__":
    sys.exit(main())
