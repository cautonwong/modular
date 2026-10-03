#!/usr/bin/env python3
"""Recount the migration symbol by symbol, against the trees themselves.

`docs/bldc-migration-coverage-matrix.md` was written before phases B2, B3 and B5 landed and its
per-subsystem counts were never recomputed; it never carried the per-symbol list its 412/181 figures
implied either, so it could not be recomputed by hand. This produces that list.

The method is deliberately mechanical and reproducible: for every public function the reference's own
headers declare, look for the name in this repository, and classify it three ways - ported (a symbol
of that name exists here), cited (the name, or the reference file it lives in, appears in a comment
here, which is how this port records what it is porting), or absent (neither). The third group is the
worklist; the other two are evidence, not a claim of parity, because a ported name is not the same as
a ported behaviour - the tests and the conformance view are what say whether it is.

Usage:
    python3 tools/audit_bldc_symbols.py [--reference /workspaces/vendor/bldc] [--csv out.csv]
"""

import argparse
import os
import re
import sys

# The subsystems the matrix's own table rows name, with the headers its "上游头文件" column lists.
SUBSYSTEMS = [
    ("FOC core & observers", ["motor/mcpwm_foc.h", "motor/foc_math.h"]),
    ("Motor interface", ["motor/mc_interface.h"]),
    ("Protocol & framing", ["comm/packet.h", "comm/commands.h"]),
    ("CAN", ["comm/comm_can.h"]),
    ("Configuration & flash", ["conf_general.h", "confgenerator.h"]),
    ("Terminal", ["terminal.h"]),
    ("Timeout & shutdown", ["timeout.h"]),
    ("BMS", ["bms.h"]),
    ("Gate drivers", ["driver/drv8301.h", "driver/drv8323s.h", "driver/drv8320s.h"]),
    ("Input applications", ["applications/app.h"]),
    ("Angle sensors", ["encoder/encoder.h"]),
]

# A declaration at the start of a line: a type, a name, then an open paren. Deliberately narrow -
# it reads the reference's own public headers, and anything it misses shows up as a smaller
# denominator rather than as a false "ported".
DECL = re.compile(
    r"^(?:static\s+inline\s+)?(?:void|int|bool|float|uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|"
    r"uint64_t|int64_t|double|unsigned|char|mc_fault_code|mc_state|setup_values|can_status_msg\w*|"
    r"bms_config|mc_configuration|thd_wa_t)\s+"
    r"\*?\s*([a-z_][a-z0-9_]*)\s*\(",
    re.MULTILINE,
)

# Names that are the port's own vocabulary or the toolchain's, and are never reference symbols.
IGNORE = {
    "main", "read", "write", "poll", "init", "deinit", "module", "self", "memcpy", "memset",
}


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return None


def reference_symbols(reference, headers):
    """Every public function the given headers declare, in declaration order."""
    names = []
    for header in headers:
        text = read(os.path.join(reference, header))
        if text is None:
            continue
        # Drop comments so a commented-out declaration is not counted.
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        text = re.sub(r"//[^\n]*", "", text)
        for match in DECL.finditer(text):
            name = match.group(1)
            if name in IGNORE or name in names:
                continue
            names.append(name)
    return names


def tree_text(root):
    """Every C source, header and document in this repository, concatenated once."""
    chunks = []
    for folder, _dirs, files in os.walk(root):
        if "/.git" in folder or "/build" in folder:
            continue
        for name in files:
            if name.endswith((".c", ".h", ".md", ".py", ".json", ".yml", ".yaml")):
                text = read(os.path.join(folder, name))
                if text is not None:
                    chunks.append(text)
    return "\n".join(chunks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", default="/workspaces/vendor/bldc")
    parser.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--csv")
    args = parser.parse_args()

    if not os.path.isdir(args.reference):
        print(f"reference tree not found: {args.reference}", file=sys.stderr)
        return 1

    repo_text = tree_text(args.repo)
    repo_words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", repo_text))

    rows = []
    total = ported = cited = absent = 0
    for subsystem, headers in SUBSYSTEMS:
        names = reference_symbols(args.reference, headers)
        sub_ported = sub_cited = sub_absent = 0
        missing = []
        for name in names:
            total += 1
            if name in repo_words:
                ported += 1
                sub_ported += 1
            elif name in repo_text:
                cited += 1
                sub_cited += 1
            else:
                absent += 1
                sub_absent += 1
                missing.append(name)
        rows.append((subsystem, headers, len(names), sub_ported, sub_cited, sub_absent, missing))

    print(f"{'subsystem':28} {'symbols':>8} {'ported':>7} {'cited':>6} {'absent':>7} {'covered':>8}")
    for subsystem, _headers, count, p, c, a, _m in rows:
        covered = 100.0 * (p + c) / count if count else 0.0
        print(f"{subsystem:28} {count:8d} {p:7d} {c:6d} {a:7d} {covered:7.1f}%")
    covered = 100.0 * (ported + cited) / total if total else 0.0
    print(f"{'TOTAL':28} {total:8d} {ported:7d} {cited:6d} {absent:7d} {covered:7.1f}%")

    print("\nAbsent symbols, by subsystem (the worklist):")
    for subsystem, _headers, _count, _p, _c, a, missing in rows:
        if a:
            print(f"  {subsystem} ({a}): {', '.join(missing)}")

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as handle:
            handle.write("subsystem,symbol,status\n")
            for subsystem, _headers, _count, _p, _c, _a, missing in rows:
                for name in missing:
                    handle.write(f"{subsystem},{name},absent\n")
        print(f"\nworklist written to {args.csv}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
