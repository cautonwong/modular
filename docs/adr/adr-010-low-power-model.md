# ADR-010: Low Power State Machine and Wake Lock Management

## Status
Accepted

## Context
Embedded devices require transitioning between active RUN, IDLE (WFI), LIGHT SLEEP, and DEEP SLEEP while preserving RAM retention, active wake sources, and preventing lost wakeups during asynchronous operations (e.g. active Flash writes, BLE radio transactions, display refreshes).

## Decision
1. **Power States**:
   - `RUN`: CPU and all configured peripherals active.
   - `IDLE`: CPU in WFI/WFE waiting for IRQ; peripheral clocks active.
   - `LIGHT_SLEEP`: High-speed clock gated; RTC/WDT/GPIO wake sources enabled; RAM retained.
   - `DEEP_SLEEP`: Power domains gated; non-essential peripherals disabled; resume via dedicated wake pins or RTC alarm.
2. **Wake Lock Management**: Subsystems acquire wake locks (`edge_pm_wake_lock_acquire`) during in-flight transactions (e.g. `EDGE_PM_LOCK_FLASH_WRITE`, `EDGE_PM_LOCK_BLE_TX`). The scheduler will not enter sleep states while wake locks remain active.
3. **Deadline Awareness**: Sleep duration is constrained by the scheduler's next due deadline (`next_deadline_ticks`), avoiding missed task periods.

## Consequences
- Clean power state transitions without interrupting background I/O transactions or missing scheduled tasks.
