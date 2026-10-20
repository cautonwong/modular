#!/usr/bin/env python3
"""Cross-runner behavioural parity for one frozen capsule (ticket #242).

Contract suites prove a port in isolation; this asks a different question: when
the *same* product capsule (same app, same drive script, same observation schema)
runs under a different runner, does it behave the same way?

Each runner writes the bounded `OBS1` log described in
`tests/integration/minimal_product/parity_capsule.h`. This script compares those
logs against a checked-in golden oracle (recorded from the reference runner) and
fails on any difference that is not explained by `ci/parity/allowlist.json`.

An allowlist entry is a semantic delta that was measured, not a silenced one, so
every entry must carry a `reason` and an `adr` (or `cause`) field. The count of
allowlisted deltas is reported on every run.

Usage:
    check_runner_parity.py [root] --log name=path --log name=path ...
                           [--golden ci/parity/minimal_capsule.golden]
                           [--allowlist ci/parity/allowlist.json]
    check_runner_parity.py [root] --record ci/parity/... --log name=path

Exit: 0 all logs match the oracle (modulo allowlisted deltas), 1 otherwise.
"""
import json
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
GOLDEN = "ci/parity/minimal_capsule.golden"
ALLOWLIST = "ci/parity/allowlist.json"
PREFIX = "OBS1 "


def parse_log(text: str) -> dict:
    """OBS1 log -> {key: value}; the schema line is keyed as 'schema'."""
    fields = {}
    for line in text.splitlines():
        if not line.startswith(PREFIX):
            continue
        parts = line[len(PREFIX) :].split()
        if len(parts) < 2:
            continue
        key = " ".join(parts[:-1])
        fields[key] = " ".join(parts[-1:])
    return fields


def load_allowlist(path: Path) -> tuple:
    """Return ({key: reason}, problems). An entry without a cause is a problem."""
    if not path.is_file():
        return {}, []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        return {}, [f"{path.name}: not valid JSON ({error.msg})"]
    entries = {}
    problems = []
    for entry in data.get("entries", []):
        key = entry.get("key")
        reason = (entry.get("reason") or "").strip()
        source = (entry.get("adr") or entry.get("cause") or "").strip()
        if not key:
            problems.append(f"{path.name}: entry without a key: {entry}")
        elif not reason or not source:
            problems.append(
                f"{path.name}: '{key}' needs a reason and an adr/cause (a measured delta, not a silence)"
            )
        else:
            entries[key] = f"{reason} [{source}]"
    return entries, problems


def compare(reference: dict, log: dict, allowlist: dict) -> tuple:
    """Return (unexplained, allowlisted) line lists for one log against the oracle."""
    unexplained = []
    explained = []
    for key in sorted(set(reference) | set(log)):
        want = reference.get(key)
        got = log.get(key)
        if want == got:
            continue
        if key in allowlist:
            explained.append(f"  allowlisted: {key}: oracle={want} log={got} ({allowlist[key]})")
            continue
        unexplained.append(f"  UNEXPLAINED: {key}: oracle={want} log={got}")
    return unexplained, explained


def main(argv) -> int:
    args = argv[1:]
    positional = [arg for arg in args if not arg.startswith("--")]
    root = Path(positional[0]).resolve() if positional else DEFAULT_ROOT

    logs = {}
    golden = GOLDEN
    allowlist_path = ALLOWLIST
    record = None
    index = 0
    while index < len(args):
        arg = args[index]
        if arg == "--log":
            name, _, path = args[index + 1].partition("=")
            logs[name] = root / path
            index += 2
            continue
        if arg == "--golden":
            golden = args[index + 1]
            index += 2
            continue
        if arg == "--allowlist":
            allowlist_path = args[index + 1]
            index += 2
            continue
        if arg == "--record":
            record = args[index + 1]
            index += 2
            continue
        index += 1

    if not logs:
        print("usage: check_runner_parity.py [root] --log name=path [--log ...]")
        return 2

    parsed = {}
    for name, path in logs.items():
        if not path.is_file():
            print(f"runner-parity: {name}: no observation log at {path}")
            return 1
        parsed[name] = parse_log(path.read_text(encoding="utf-8"))
        if "schema" not in parsed[name]:
            print(f"runner-parity: {name}: log has no '{PREFIX}schema' line")
            return 1

    schemas = {name: fields["schema"] for name, fields in parsed.items()}
    if len(set(schemas.values())) > 1:
        print(f"runner-parity: runners disagree on the observation schema: {schemas}")
        return 1

    if record is not None:
        name = next(iter(logs))
        target = root / record
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(logs[name].read_text(encoding="utf-8"), encoding="utf-8")
        print(f"runner-parity: recorded the oracle from '{name}' into {record}")
        return 0

    golden_path = root / golden
    if not golden_path.is_file():
        print(f"runner-parity: missing golden oracle {golden}")
        return 1
    reference = parse_log(golden_path.read_text(encoding="utf-8"))
    if reference.get("schema") != schemas[next(iter(schemas))]:
        print(
            f"runner-parity: golden oracle is schema {reference.get('schema')}, "
            f"runners are schema {schemas[next(iter(schemas))]}"
        )
        return 1

    allowlist, problems = load_allowlist(root / allowlist_path)
    exit_code = 1 if problems else 0
    for problem in problems:
        print(f"runner-parity: bad allowlist: {problem}")

    explained_total = 0
    for name in sorted(parsed):
        unexplained, explained = compare(reference, parsed[name], allowlist)
        explained_total += len(explained)
        if unexplained:
            print(f"runner-parity: {name} differs from the oracle ({golden}):")
            for line in unexplained + explained:
                print(line)
            exit_code = 1
        elif explained:
            print(f"runner-parity: {name} matches the oracle within the allowlist:")
            for line in explained:
                print(line)

    if exit_code == 0:
        print(
            f"runner-parity: PASS (schema {reference.get('schema')}, "
            f"{len(parsed)} runners, {len(reference)} observed fields, "
            f"{explained_total} allowlisted delta(s))"
        )
    else:
        print("runner-parity: FAIL - an unexplained difference is a behavioural change, not noise")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
