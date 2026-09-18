#!/usr/bin/env python3
"""Check commit messages against Conventional Commits (CONTRIBUTING.md).

Three modes:

    check_commit_messages.py --range <revspec>    # every commit in a git range
    check_commit_messages.py --file <path>        # one message file (commit-msg hook)
    check_commit_messages.py <messages-file>      # fixture file, records split by '==='

Exemptions (never a failure):

* merge commits (more than one parent, or a `Merge ` subject);
* dependabot commits (author or subject identifies dependabot);
* GitHub-generated reverts (`Revert "..."`).

The allowed types and the subject length limit match what CONTRIBUTING.md
documents, so the checked rule is the written rule.

Usage: check_commit_messages.py [--range R | --file P | MESSAGES_FILE]
"""
from pathlib import Path
import re
import subprocess
import sys

TYPES = ("feat", "fix", "build", "ci", "docs", "test", "refactor", "perf", "chore", "style", "revert")
SUBJECT_LIMIT = 100  # matches the repo's column limit; the longest existing subject is 95
HEADER = re.compile(r"^(" + "|".join(TYPES) + r")(\([^()]+\))?!?: \S")
SEPARATOR = re.compile(r"^===\s*$", re.M)


def is_exempt(subject: str, author: str, parents: int) -> bool:
    if parents > 1:
        return True
    if subject.startswith("Merge "):
        return True
    if subject.startswith('Revert "'):
        return True
    lowered = f"{subject} {author}".lower()
    return "dependabot" in lowered or subject.startswith("Bump ")


def check_message(subject: str, author: str = "", parents: int = 0) -> list:
    if is_exempt(subject, author, parents):
        return []
    problems = []
    if not HEADER.match(subject):
        problems.append(
            f"not a Conventional Commit: {subject!r} "
            f"(expected 'type(scope): subject' with type in {', '.join(TYPES)})"
        )
    if len(subject) > SUBJECT_LIMIT:
        problems.append(f"subject is {len(subject)} chars, limit is {SUBJECT_LIMIT}: {subject!r}")
    return problems


def from_range(revspec: str) -> list:
    fmt = "%s%x1f%an <%ae>%x1f%P%x1e"
    try:
        raw = subprocess.run(
            ["git", "log", f"--format={fmt}", revspec],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except subprocess.CalledProcessError as exc:
        return [f"git log {revspec} failed: {exc.stderr.strip()}"]
    problems = []
    for record in raw.split("\x1e"):
        record = record.strip("\n")
        if not record:
            continue
        subject, author, parents = (record.split("\x1f") + ["", ""])[:3]
        problems.extend(f"{subject!r}: {p}" for p in check_message(subject, author, len(parents.split())))
    return problems


def from_file(path: Path) -> list:
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []
    for record in SEPARATOR.split(text):
        record = record.strip("\n")
        if not record:
            continue
        problems.extend(f"{record.splitlines()[0]!r}: {p}" for p in check_message(record.splitlines()[0]))
    return problems


def check(target: Path) -> tuple:
    problems = from_file(target)
    if problems:
        return (1, problems)
    return (0, [f"commit message check: PASS ({target.name})"])


def main(argv) -> int:
    if len(argv) > 2 and argv[1] == "--range":
        problems = from_range(argv[2])
    elif len(argv) > 2 and argv[1] == "--file":
        problems = from_file(Path(argv[2]))
    elif len(argv) > 1:
        problems = from_file(Path(argv[1]))
    else:
        print(__doc__)
        return 2
    for problem in problems:
        print(f"  {problem}" if not problem.startswith("git log") else problem)
    if problems:
        print("commit message check: FAIL")
        return 1
    print("commit message check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
