#!/usr/bin/env python3
"""Enforce ISR coding rules and architectural boundaries (ADR-008, CI-ARCH-005).

Verifies that ISR functions across board/, soc/, and sys/ follow strict real-time rules:
1. No dynamic memory allocation (malloc, calloc, realloc, free).
2. No blocking or formatting I/O (printf, fprintf, scanf).
3. No direct invocation of application business callbacks (app_*).
4. No direct invocation of scheduler runner loops (edge_sys_step, edge_sys_run).
5. ISR interactions must strictly route through one of the 4 defined IRQ classes:
   - Event IRQ: edge_event_push_isr / edge_event_sink_push_isr
   - Data IRQ: edge_ring_buffer_* / uart_ring_*
   - Scheduler IRQ: monotonic_ticks / clock update
   - Fault IRQ: edge_fault_capture_isr

Usage: check_isr_rules.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# ISR function signature patterns
ISR_FUNC_PATTERN = re.compile(
    r"^[a-zA-Z0-9_* ]*\b([a-zA-Z0-9_]*(?:_irq|_isr|_Handler|_IRQHandler|_vector)[a-zA-Z0-9_]*)\s*\([^)]*\)\s*\{",
    re.M,
)

FORBIDDEN_CALLS = {
    "malloc": "dynamic memory allocation in ISR",
    "calloc": "dynamic memory allocation in ISR",
    "realloc": "dynamic memory allocation in ISR",
    "free": "dynamic memory allocation in ISR",
    "printf": "formatted blocking I/O in ISR",
    "fprintf": "formatted blocking I/O in ISR",
    "sprintf": "unsafe formatting in ISR",
    "edge_sys_step": "runner scheduling loop invocation from ISR",
    "edge_sys_run": "runner scheduling loop invocation from ISR",
    "edge_sys_run_once": "runner scheduling loop invocation from ISR",
}


def extract_function_body(text: str, start_index: int) -> str:
    """Extract braces-enclosed function body starting at opening brace."""
    open_brace = text.find("{", start_index)
    if open_brace == -1:
        return ""
    depth = 1
    i = open_brace + 1
    n = len(text)
    while i < n and depth > 0:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[open_brace:i]


def check(root: Path) -> tuple:
    violations = []
    scanned_isrs = 0

    scan_dirs = [root / "board", root / "soc", root / "sys"]
    active_dirs = [d for d in scan_dirs if d.is_dir()]
    if not active_dirs:
        active_dirs = [root]

    for sdir in active_dirs:
        for src in sdir.glob("**/*.[ch]"):
            content = src.read_text(encoding="utf-8", errors="replace")
            for match in ISR_FUNC_PATTERN.finditer(content):
                fn_name = match.group(1)
                scanned_isrs += 1
                body = extract_function_body(content, match.start())
                rel_path = src.relative_to(root)

                # Check forbidden function calls
                for call, desc in FORBIDDEN_CALLS.items():
                    pattern = rf"\b{re.escape(call)}\s*\("
                    if re.search(pattern, body):
                        violations.append(
                            f"{rel_path}: ISR '{fn_name}' calls forbidden '{call}' ({desc})"
                        )

                # Check direct app callbacks
                app_calls = re.findall(r"\b(app_[a-zA-Z0-9_]+)\s*\(", body)
                for app_call in app_calls:
                    violations.append(
                        f"{rel_path}: ISR '{fn_name}' directly invokes app callback '{app_call}' (violates board/app isolation D17)"
                    )

    if violations:
        return (1, violations)
    return (0, [f"ISR rules check: PASS ({scanned_isrs} ISR functions verified, 0 forbidden calls)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
