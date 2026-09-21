#!/usr/bin/env python3
"""Check commit messages against Conventional Commits (CONTRIBUTING.md).

Three modes:

    check_commit_messages.py --range <revspec>    # every commit in a git range
    check_commit_messages.py --file <path>        # one message file (commit-msg hook)
    check_commit_messages.py <messages-file>      # fixture file, records split by '==='

The `--range` mode sees what the branch is *now*. On a pull request it cannot see
what `main` will get: a squash merge rewrites the branch commits into one subject
built from the **pull request title plus ` (#<number>)`**. A 97-character title
passes every check on the branch and fails on `main` with 104, which is exactly
how this repository went red once. So a fourth mode checks the prospective squash
subject instead:

    check_commit_messages.py --pr-title <title> --pr-number <number>

Exemptions (never a failure):

* merge commits (more than one parent, or a `Merge ` subject);
* dependabot commits (author or subject identifies dependabot);
* GitHub-generated reverts (`Revert "..."`).

Reference rule: every commit must carry a ticket reference `(#NN)` in the
subject, so `git log` can be traced back to the tracker without hand-written
notes. The documented whitelist is by type: `chore`, `docs` and `style` may omit
it (a typo fix or a note does not need a ticket).

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
REFERENCE = re.compile(r"\(#\d+\)")
# Types that may omit the ticket reference (a typo fix or a note needs no ticket).
REFERENCE_OPTIONAL = ("chore", "docs", "style")
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
    match = HEADER.match(subject)
    if not match:
        problems.append(
            f"not a Conventional Commit: {subject!r} "
            f"(expected 'type(scope): subject' with type in {', '.join(TYPES)})"
        )
    elif match.group(1) not in REFERENCE_OPTIONAL and not REFERENCE.search(subject):
        problems.append(
            f"no ticket reference: {subject!r} "
            f"(add '(#NN)'; only {'/'.join(REFERENCE_OPTIONAL)} may omit it)"
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


def squash_subject(title: str, number: str) -> str:
    """What GitHub writes on `main`: the pull request title plus ' (#<number>)'."""
    return f"{title} (#{number})"


def from_pr_title(title: str, number: str) -> list:
    """Validate the subject the *squash merge* will create, not the branch's own."""
    subject = squash_subject(title, number)
    return [f"the squash subject would be {subject!r}: {p}" for p in check_message(subject)]


def check(target: Path) -> tuple:
    problems = from_file(target)
    if problems:
        return (1, problems)
    return (0, [f"commit message check: PASS ({target.name})"])


def main(argv) -> int:
    # Options are scanned rather than positional: the guard self-test passes the
    # fixture root first, then the extra arguments, so a fixed position would not
    # survive both callers.
    args = argv[1:]
    if "--pr-title" in args and "--pr-number" in args:
        problems = from_pr_title(args[args.index("--pr-title") + 1], args[args.index("--pr-number") + 1])
    elif "--range" in args:
        problems = from_range(args[args.index("--range") + 1])
    elif "--file" in args:
        problems = from_file(Path(args[args.index("--file") + 1]))
    else:
        positional = [a for a in args if not a.startswith("--")]
        if not positional:
            print(__doc__)
            return 2
        problems = from_file(Path(positional[0]))
    for problem in problems:
        print(f"  {problem}" if not problem.startswith("git log") else problem)
    if problems:
        print("commit message check: FAIL")
        return 1
    print("commit message check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
