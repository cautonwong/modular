#!/usr/bin/env python3
"""Emit reproducible-build provenance metadata as JSON."""
import datetime
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCIES = ROOT / "ci/dependencies.json"


def dependencies():
    """The pinned third-party components (#89): name, tag and the commit the build verified."""
    if not DEPENDENCIES.is_file():
        return []
    document = json.loads(DEPENDENCIES.read_text(encoding="utf-8"))
    return [
        {
            "name": entry["name"],
            "version": entry.get("version", "unknown"),
            "commit": entry.get("commit", "unknown"),
            "licence": entry.get("licence", "unknown"),
            "url": entry.get("url", ""),
        }
        for entry in document.get("dependencies", [])
    ]


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def first_line(text):
    lines = text.splitlines()
    return lines[0] if lines else "unknown"


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "build-metadata.json"
    cc = os.environ.get("CC", "cc")
    metadata = {
        "project": "edge-modular",
        "description": run(["git", "describe", "--tags", "--always"]) or "unknown",
        "commit": run(["git", "rev-parse", "HEAD"]) or "unknown",
        "ref": os.environ.get("GITHUB_REF", run(["git", "rev-parse", "--abbrev-ref", "HEAD"])),
        "source_date_epoch": os.environ.get("SOURCE_DATE_EPOCH", ""),
        "cc": cc,
        "compiler": first_line(run([cc, "--version"])),
        "cmake": first_line(run(["cmake", "--version"])),
        "runner_os": os.environ.get("RUNNER_OS", platform.system()),
        "python": platform.python_version(),
        "dependencies": dependencies(),
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
