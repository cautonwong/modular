#!/usr/bin/env python3
"""Enforce RTOS ISR Context Safety (Issue #85, D71/D75/D85).

Rules:
1. Zero Blocking/Allocating Calls in Interrupt Handlers (中断上下文零阻塞与零分配原则):
   Hardware ISR routines (in `board/*/src/*.c` and functions matching `_irq` or `_isr`)
   must never call:
   - Blocking delays: `vTaskDelay`, `tx_thread_sleep`, `k_sleep`, `sleep`, `usleep`
   - Dynamic allocations: `malloc`, `calloc`, `free`
   - Task creation / scheduler locks: `xTaskCreate`, `tx_thread_create`, `k_thread_create`
   - Blocking mutexes / semaphores: `xSemaphoreTake`, `tx_mutex_get`, `k_mutex_lock`
   - Blocking I/O: `printf`, `fprintf`

2. Permitted RTOS Bridge in ISR (唯一允许的中断-RTOS桥梁):
   The only permitted RTOS wake call inside an ISR/sink-guard path is `edge_rtos_wake_from_isr()`.
   ISRs must remain RTOS-free (D85), only clearing flags, capturing scalars, pushing to
   the event sink, and optionally triggering conditional wake via `edge_rtos_wake_from_isr()`.

Usage: check_rtos_isr_safety.py [root]
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Banned API calls inside interrupt handlers
BANNED_ISR_CALLS = re.compile(
    r'\b(?:'
    r'vTaskDelay|vTaskDelayUntil|tx_thread_sleep|k_sleep|'
    r'malloc|calloc|realloc|free|'
    r'xTaskCreate|xTaskCreateStatic|tx_thread_create|k_thread_create|'
    r'xSemaphoreTake|tx_mutex_get|k_mutex_lock|'
    r'printf|fprintf'
    r')\s*\(',
    re.M,
)

# Detect ISR function definitions (functions containing 'irq' or 'isr' in name)
ISR_FUNC_DEF = re.compile(
    r'^(?:static\s+)?(?:void|edge_status_t|uint32_t|int)\s+([a-zA-Z0-9_]*(?:irq|isr)[a-zA-Z0-9_]*)\s*\([^)]*\)\s*\{',
    re.M | re.I,
)


def extract_function_body(text: str, start_pos: int) -> tuple:
    """Extract block delimited by braces starting at start_pos."""
    brace_depth = 0
    in_str = False
    in_char = False
    escape = False
    i = start_pos
    body_start = -1

    while i < len(text):
        c = text[i]
        if escape:
            escape = False
            i += 1
            continue
        if c == "\\":
            escape = True
            i += 1
            continue
        if c == '"' and not in_char:
            in_str = not in_str
        elif c == "'" and not in_str:
            in_char = not in_char
        elif not in_str and not in_char:
            if c == "{":
                if brace_depth == 0:
                    body_start = i
                brace_depth += 1
            elif c == "}":
                brace_depth -= 1
                if brace_depth == 0:
                    return text[body_start : i + 1], i + 1
        i += 1
    return "", len(text)


def check_source_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    # Find all ISR functions in the file
    for match in ISR_FUNC_DEF.finditer(text):
        func_name = match.group(1)
        line_no = text[: match.start()].count("\n") + 1
        body, _end = extract_function_body(text, match.start())
        if not body:
            continue

        for banned in BANNED_ISR_CALLS.finditer(body):
            call_line = line_no + body[: banned.start()].count("\n")
            call_name = banned.group(0).rstrip("(")
            problems.append(
                f"{rel}:{call_line}: banned call '{call_name}()' inside ISR function '{func_name}' "
                f"(Issue #85: ISRs must be non-blocking and zero-allocation)"
            )

    return problems


def check(root: Path) -> list:
    root = root.resolve()
    problems = []

    # Board directory ISRs
    board_dir = root / "board"
    if board_dir.is_dir():
        for src_path in board_dir.rglob("*.c"):
            problems.extend(check_source_file(src_path, root))

    return problems


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print(f"RTOS ISR safety: FAIL ({len(problems)} problem(s)):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    print("RTOS ISR safety: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
