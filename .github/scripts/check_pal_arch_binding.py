#!/usr/bin/env python3
"""Cortex-M PAL architecture binding (defect #128).

`pal/cortex-m-bare` has a real ARM implementation and a host fallback. If the
fallback could be compiled into a non-ARM *target*, the port would build, link
and pass tests while its "monotonic clock" reported call counts instead of time.
The source therefore refuses to compile without either `__arm__` or the explicit
host-test macro, and this gate proves the refusal still works.

It compiles the port twice with the host compiler and requires:

* without `EDGE_PAL_CORTEX_M_HOST_TEST` the compile FAILS with the port's own
  message (not for some unrelated reason);
* with the macro the compile SUCCEEDS;
* the source still carries the refusal (so a silent deletion is caught even if
  the compile would still fail for a different cause).

The negative fixture is a skeleton with the refusal removed; it must make this
checker fail.

Usage: check_pal_arch_binding.py [root]
"""
from pathlib import Path
import os
import subprocess
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
SOURCE = "pal/cortex-m-bare/src/pal_cortex_m.c"
INCLUDE_DIRS = ("pal/cortex-m-bare/include", "edge_module/include")
HOST_MACRO = "EDGE_PAL_CORTEX_M_HOST_TEST"
REFUSAL = "#error"
ARCH_TOKEN = "__arm__"


def compile_source(root: Path, define: str | None) -> subprocess.CompletedProcess:
    cc = os.environ.get("CC", "cc")
    argv = [cc, "-fsyntax-only", "-std=c11"]
    if define is not None:
        argv.append(f"-D{define}=1")
    argv.extend(f"-I{root / inc}" for inc in INCLUDE_DIRS)
    argv.append(str(root / SOURCE))
    return subprocess.run(argv, capture_output=True, text=True)


def last_diagnostic(stderr: str) -> str:
    lines = [line for line in stderr.strip().splitlines() if line.strip()]
    return lines[-1] if lines else "no diagnostics"


def check(root: Path) -> tuple:
    root = root.resolve()
    source = root / SOURCE
    if not source.is_file():
        return (1, [f"missing {SOURCE}"])

    text = source.read_text(encoding="utf-8", errors="replace")
    problems = []
    for marker in (REFUSAL, HOST_MACRO, ARCH_TOKEN):
        if marker not in text:
            problems.append(f"{SOURCE} no longer mentions '{marker}': the architecture binding is gone")

    plain = compile_source(root, None)
    if plain.returncode == 0:
        problems.append(
            "compiling without the host-test macro succeeded: a non-ARM target would "
            "silently link the fake clock"
        )
    elif "pal/cortex-m-bare" not in plain.stderr:
        problems.append(f"the compile failed for an unrelated reason: {last_diagnostic(plain.stderr)}")

    with_macro = compile_source(root, HOST_MACRO)
    if with_macro.returncode != 0:
        problems.append(f"compiling with -D{HOST_MACRO} failed: {last_diagnostic(with_macro.stderr)}")

    if problems:
        return (1, problems)
    return (
        0,
        [
            f"pal arch binding: PASS ({SOURCE} refuses non-ARM without -D{HOST_MACRO})",
        ],
    )


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    stream = sys.stdout if code == 0 else sys.stderr
    for message in messages:
        print(message, file=stream)
    return code


if __name__ == "__main__":
    sys.exit(main())
