#!/usr/bin/env python3
"""Keep `docs/adr-conformance.md` honest as a machine-checked ledger (ticket #244).

The conformance table is the only decision-vs-implementation view, and it is
hand-edited under high merge velocity. This guard checks the parts of it a
script can prove, so a row cannot silently drift from the decision source, the
tree, or the issue tracker.

Rules (all already true of a healthy row, so the gate cannot drift from the
written convention):

* every ADR id in a row exists in `docs/adr.md`;
* every row's status is one of the legend marks (✅ 🟡 ❌ ⛔ 🚫) - free text
  like "done" or "OK" is how a row stops being readable by both humans and
  this script;
* every path-shaped token in the evidence cell (a backticked token containing
  a `/`) resolves to a tracked file or directory at HEAD. Bare names such as
  `errors.h` or `FreeRTOS.h` are symbol citations, not paths, so they are left
  to review;
* with `GITHUB_TOKEN` (and `gh`) available, ✅ rows must have only closed
  linked issues, and open-ish rows (🟡 ❌ ⛔) need at least one open linked
  issue or a `deferred:` / `tracked-out-of-band` token. Without the token the
  issue-state rules are skipped and reported as such, so forks stay green.

Escape hatch: a row may carry `<!-- conformance:ignore reason=... -->`, which
skips the row and is counted in the report.

Usage: check_conformance_ledger.py [root] [--no-issues]
"""
import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
CONFORMANCE = "docs/adr-conformance.md"
ADR = "docs/adr.md"

LEGEND = {"✅", "🟡", "❌", "⛔", "🚫"}
OPENISH = {"🟡", "❌", "⛔"}
DEFERRAL = re.compile(r"(?i)(?:\bdeferred:|\btracked-out-of-band\b)")
IGNORE = re.compile(r"<!--\s*conformance:ignore\s+reason=([^>]*?)\s*-->")
ADR_ID = re.compile(r"\bD(\d+)\b")
ADR_RANGE = re.compile(r"\bD(\d+)\s*[–-]\s*D(\d+)\b")
ISSUE_REF = re.compile(r"#(\d+)")
BACKTICK = re.compile(r"`([^`]+)`")

NOT_A_PATH = re.compile(r"[*?\[\]{}()<>|\s]")


def tracked_paths(root: Path) -> list:
    """Repo paths at HEAD plus untracked-but-not-ignored files, relative to root."""
    try:
        result = subprocess.run(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=root,
            capture_output=True,
            text=True,
            timeout=60,
        )
        if result.returncode == 0:
            return [path for path in result.stdout.split("\0") if path]
    except (OSError, subprocess.SubprocessError):
        pass
    skip = {".git", "build", "vendor", "node_modules"}
    paths = []
    for path in root.rglob("*"):
        if skip.intersection(path.relative_to(root).parts):
            continue
        if path.is_file():
            paths.append(path.relative_to(root).as_posix())
    return paths


def adr_ids(cell: str) -> set:
    ids = {f"D{number}" for number in ADR_ID.findall(cell)}
    for low, high in ADR_RANGE.findall(cell):
        ids.update(f"D{n}" for n in range(int(low), int(high) + 1))
    return ids


def evidence_paths(cell: str) -> list:
    paths = []
    for token in BACKTICK.findall(cell):
        token = token.strip()
        if NOT_A_PATH.search(token) or token.startswith(("http://", "https://")):
            continue
        if "/" in token:
            paths.append(token.rstrip("/"))
    return paths


def exists_in_tree(token: str, tree: list) -> bool:
    return any(path == token or path.endswith("/" + token) or path.startswith(token + "/") for path in tree)


def rows(text: str):
    """Yield (line_number, adr_cell, status_cell, evidence_cell) for table rows."""
    for number, line in enumerate(text.splitlines(), 1):
        if not line.lstrip().startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 2 or not re.match(r"^D\d", cells[0]):
            continue
        yield number, cells[0], (cells[2] if len(cells) > 2 else ""), " ".join(cells[3:])


def issue_state(number: str):
    """Return 'open' / 'closed' / None when the state cannot be read."""
    try:
        result = subprocess.run(
            ["gh", "issue", "view", number, "--json", "state", "--jq", ".state"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    state = result.stdout.strip().lower()
    return state if result.returncode == 0 and state in {"open", "closed"} else None


def check(root: Path, use_issues: bool = True) -> tuple:
    conformance = root / CONFORMANCE
    if not conformance.is_file():
        return (1, [f"missing {CONFORMANCE}"])
    adr_path = root / ADR
    if not adr_path.is_file():
        return (1, [f"missing {ADR}"])

    adr_text = adr_path.read_text(encoding="utf-8")
    known = adr_ids(adr_text)
    text = conformance.read_text(encoding="utf-8")
    tree = tracked_paths(root)

    problems = []
    ignored = []
    row_count = 0
    issue_checked = 0
    for number, adr_cell, status_cell, evidence_cell in rows(text):
        row_count += 1
        where = f"{CONFORMANCE}:{number}"
        row_text = f"{adr_cell} | {status_cell} | {evidence_cell}"

        ignore = IGNORE.search(row_text)
        if ignore:
            ignored.append(f"{adr_cell}: {ignore.group(1)}")
            continue

        marks = {mark for mark in LEGEND if mark in status_cell}
        if not marks:
            problems.append(f"{where}: no legend status in {status_cell!r}")
            continue
        if len(marks) > 1:
            problems.append(f"{where}: conflicting status marks {sorted(marks)}")

        for adr in sorted(adr_ids(adr_cell)):
            if adr not in known:
                problems.append(f"{where}: {adr} is not a decision in {ADR}")

        for path in evidence_paths(evidence_cell):
            if not exists_in_tree(path, tree):
                problems.append(f"{where}: evidence path not in the tree: {path}")

        if not use_issues:
            continue
        refs = sorted(set(ISSUE_REF.findall(row_text)))
        if not refs or DEFERRAL.search(row_text):
            continue
        states = [issue_state(ref) for ref in refs]
        if None in states:
            continue
        issue_checked += 1
        linked = ", ".join(
            f"#{ref} {state}" for ref, state in zip(refs, states, strict=True)
        )
        if marks == {"✅"} and "open" in states:
            problems.append(f"{where}: status ✅ but a linked issue is open ({linked})")
        elif marks & OPENISH and "open" not in states:
            problems.append(f"{where}: status {''.join(marks)} but every linked issue is closed ({linked})")

    header = (
        f"conformance ledger: {row_count} rows, {len(ignored)} ignored, "
        f"{issue_checked} issue-checked"
    )
    if problems:
        return (1, [header] + problems)
    messages = [f"conformance ledger: PASS ({header.split(': ')[1]})"]
    if use_issues:
        messages.append("issue state: checked against GitHub")
    else:
        messages.append("issue state: not checked (no GITHUB_TOKEN); path/status/ADR only")
    if ignored:
        messages.append("ignored rows: " + "; ".join(ignored))
    return (0, messages)


def main(argv) -> int:
    args = [arg for arg in argv[1:] if not arg.startswith("--")]
    root = Path(args[0]).resolve() if args else DEFAULT_ROOT
    use_issues = "--no-issues" not in argv and bool(os.environ.get("GITHUB_TOKEN"))
    code, messages = check(root, use_issues=use_issues)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
