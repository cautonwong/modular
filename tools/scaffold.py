#!/usr/bin/env python3
"""Architecture Scaffolding Engine (Issue #117).

Generates 100% architecture-compliant component skeletons for:
- apps (Clean Architecture aggregate root, isolated ports.h, pure state machine)
- infra (Driver model, port contract implementation, zero soc inclusion)
- soc (SoC memory map, IRQ numbers, register drivers, zero edge_module inclusion)
- boards (Board contract, vector table, board_irq_attach, power/watchdog stubs)
- products (Composition root, static memory allocation, glue adapters)

All generated skeletons pass all 28+ repository quality and architecture gates out of the box.

Usage:
    tools/scaffold.py app <name> [--root <dir>]
    tools/scaffold.py infra <name> [--root <dir>]
    tools/scaffold.py soc <name> [--root <dir>]
    tools/scaffold.py board <name> --soc <soc> [--root <dir>]
    tools/scaffold.py product <name> --board <board> [--family <family>] [--app <app>] [--root <dir>]
"""
import argparse
import sys
from pathlib import Path


def scaffold_app(name: str, root: Path, dry_run: bool = False) -> list:
    name_clean = name.lower().replace("-", "_")
    name_upper = name_clean.upper()
    app_dir = root / "app" / name_clean
    created_files = []

    files = {
        app_dir / "CMakeLists.txt": f"""edge_add_app({name_clean}
    SOURCES
        src/{name_clean}.c
    INCLUDES
        include
    DEPS
        edge_module
)
""",
        app_dir / "include" / name_clean / "ports.h": f"""#ifndef APP_{name_upper}_PORTS_H
#define APP_{name_upper}_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

/*
 * Consumer-defined port for {name_clean} (D14).
 * Always carries 'void *self' manual this-pointer and returns edge_status_t.
 */
typedef struct {name_clean}_sink_if {{
    void *self;
    edge_status_t (*emit)(void *self, uint32_t val);
}} {name_clean}_sink_if_t;

#ifdef __cplusplus
}}
#endif

#endif /* APP_{name_upper}_PORTS_H */
""",
        app_dir / "include" / name_clean / f"{name_clean}.h": f"""#ifndef APP_{name_upper}_H
#define APP_{name_upper}_H

#include "{name_clean}/ports.h"
#include "edge/desc.h"
#include "edge/errors.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

typedef struct {name_clean}_config {{
    uint32_t max_limit;
}} {name_clean}_config_t;

typedef struct {name_clean}_instance {{
    edge_module_t module;
    const {name_clean}_sink_if_t *sink;
    {name_clean}_config_t config;
    uint32_t current_val;
    bool active;
}} {name_clean}_instance_t;

/* Invariant validators */
bool {name_clean}_validate_config(const {name_clean}_config_t *cfg);

/* Snapshot readers */
edge_status_t {name_clean}_get_config(const {name_clean}_instance_t *self, {name_clean}_config_t *out_cfg);

/* Bounded modifiers */
edge_status_t {name_clean}_set_val({name_clean}_instance_t *self, uint32_t val);

/* Lifecycle */
void {name_clean}_construct({name_clean}_instance_t *self, uint32_t module_id, uint32_t priority,
                     const {name_clean}_sink_if_t *sink, const {name_clean}_config_t *cfg);
edge_status_t {name_clean}_init({name_clean}_instance_t *self);
edge_status_t {name_clean}_shutdown({name_clean}_instance_t *self);
edge_module_t *{name_clean}_module({name_clean}_instance_t *self);

#ifdef __cplusplus
}}
#endif

#endif /* APP_{name_upper}_H */
""",
        app_dir / "src" / f"{name_clean}.c": f"""#include "{name_clean}/{name_clean}.h"
#include <string.h>

bool {name_clean}_validate_config(const {name_clean}_config_t *cfg) {{
    if (cfg == NULL) {{
        return false;
    }}
    if (cfg->max_limit == 0u) {{
        return false;
    }}
    return true;
}}

edge_status_t {name_clean}_get_config(const {name_clean}_instance_t *self, {name_clean}_config_t *out_cfg) {{
    if (self == NULL || out_cfg == NULL) {{
        return EDGE_EINVAL;
    }}
    *out_cfg = self->config;
    return EDGE_OK;
}}

edge_status_t {name_clean}_set_val({name_clean}_instance_t *self, uint32_t val) {{
    if (self == NULL) {{
        return EDGE_EINVAL;
    }}
    if (val > self->config.max_limit) {{
        return EDGE_EINVAL;
    }}
    self->current_val = val;
    if (self->sink && self->sink->emit) {{
        return self->sink->emit(self->sink->self, val);
    }}
    return EDGE_OK;
}}

static edge_status_t {name_clean}_poll(edge_module_t *mod) {{
    {name_clean}_instance_t *inst = ({name_clean}_instance_t *)edge_module_data(mod);
    if (inst == NULL) {{
        return EDGE_EINVAL;
    }}
    return EDGE_OK;
}}

static edge_status_t {name_clean}_on_event(edge_module_t *mod, const edge_event_t *ev) {{
    {name_clean}_instance_t *inst = ({name_clean}_instance_t *)edge_module_data(mod);
    if (inst == NULL || ev == NULL) {{
        return EDGE_EINVAL;
    }}
    return EDGE_OK;
}}

static edge_status_t {name_clean}_power_off(edge_module_t *mod) {{
    {name_clean}_instance_t *inst = ({name_clean}_instance_t *)edge_module_data(mod);
    if (inst == NULL) {{
        return EDGE_EINVAL;
    }}
    inst->active = false;
    return EDGE_OK;
}}

void {name_clean}_construct({name_clean}_instance_t *self, uint32_t module_id, uint32_t priority,
                     const {name_clean}_sink_if_t *sink, const {name_clean}_config_t *cfg) {{
    if (self == NULL) {{
        return;
    }}
    memset(self, 0, sizeof(*self));
    self->sink = sink;
    if (cfg) {{
        self->config = *cfg;
    }}
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.poll = {name_clean}_poll;
    self->module.on_event = {name_clean}_on_event;
    self->module.power_off = {name_clean}_power_off;
    self->module.private_data = self;
}}

edge_status_t {name_clean}_init({name_clean}_instance_t *self) {{
    if (self == NULL) {{
        return EDGE_EINVAL;
    }}
    if (!{name_clean}_validate_config(&self->config)) {{
        return EDGE_EINVAL;
    }}
    self->active = true;
    return EDGE_OK;
}}

edge_status_t {name_clean}_shutdown({name_clean}_instance_t *self) {{
    if (self == NULL) {{
        return EDGE_EINVAL;
    }}
    self->active = false;
    return EDGE_OK;
}}

edge_module_t *{name_clean}_module({name_clean}_instance_t *self) {{
    return self ? &self->module : NULL;
}}
""",
    }

    for path, content in files.items():
        if not dry_run:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        created_files.append(path)

    return created_files


def scaffold_infra(name: str, root: Path, dry_run: bool = False) -> list:
    name_clean = name.lower().replace("-", "_")
    name_upper = name_clean.upper()
    infra_dir = root / "infra" / name_clean
    created_files = []

    files = {
        infra_dir / "CMakeLists.txt": f"""edge_add_infra({name_clean}
    SOURCES
        src/{name_clean}.c
    INCLUDES
        include
    DEPS
        edge_module
)
""",
        infra_dir / "include" / name_clean / f"{name_clean}.h": f"""#ifndef INFRA_{name_upper}_H
#define INFRA_{name_upper}_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

typedef struct {name_clean}_dev {{
    uint32_t base_addr;
    bool initialized;
}} {name_clean}_dev_t;

edge_status_t {name_clean}_init({name_clean}_dev_t *dev, uint32_t base_addr);
edge_status_t {name_clean}_write(void *self, const void *buf, size_t len);
edge_status_t {name_clean}_read(void *self, void *buf, size_t len);

#ifdef __cplusplus
}}
#endif

#endif /* INFRA_{name_upper}_H */
""",
        infra_dir / "src" / f"{name_clean}.c": f"""#include "{name_clean}/{name_clean}.h"
#include <string.h>

edge_status_t {name_clean}_init({name_clean}_dev_t *dev, uint32_t base_addr) {{
    if (dev == NULL) {{
        return EDGE_EINVAL;
    }}
    dev->base_addr = base_addr;
    dev->initialized = true;
    return EDGE_OK;
}}

edge_status_t {name_clean}_write(void *self, const void *buf, size_t len) {{
    if (self == NULL) {{
        return EDGE_EINVAL;
    }}
    if (len == 0u) {{
        return EDGE_OK;
    }}
    if (buf == NULL) {{
        return EDGE_EINVAL;
    }}
    return EDGE_OK;
}}

edge_status_t {name_clean}_read(void *self, void *buf, size_t len) {{
    if (self == NULL) {{
        return EDGE_EINVAL;
    }}
    if (len == 0u) {{
        return EDGE_OK;
    }}
    if (buf == NULL) {{
        return EDGE_EINVAL;
    }}
    memset(buf, 0, len);
    return EDGE_OK;
}}
""",
    }

    for path, content in files.items():
        if not dry_run:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        created_files.append(path)

    return created_files


def scaffold_soc(name: str, root: Path, dry_run: bool = False) -> list:
    name_clean = name.lower().replace("-", "_")
    name_upper = name_clean.upper()
    soc_dir = root / "soc" / name_clean
    created_files = []

    files = {
        soc_dir / "CMakeLists.txt": f"""edge_add_soc({name_clean}
    SOURCES
        src/soc_{name_clean}.c
    DEPS
        edge_module
)
""",
        soc_dir / "include" / f"soc_{name_clean}" / f"soc_{name_clean}.h": f"""#ifndef SOC_{name_upper}_H
#define SOC_{name_upper}_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

/* Peripheral base address definitions */
#define SOC_{name_upper}_PERIPH_BASE ((uintptr_t)0x40000000u)
#define SOC_{name_upper}_TIMER0_IRQ  16u
#define SOC_{name_upper}_UART0_IRQ   17u

/* SoC lifecycle */
void soc_{name_clean}_init(void);
void soc_{name_clean}_reset(void);

#ifdef __cplusplus
}}
#endif

#endif /* SOC_{name_upper}_H */
""",
        soc_dir / "src" / f"soc_{name_clean}.c": f"""#include "soc_{name_clean}/soc_{name_clean}.h"

void soc_{name_clean}_init(void) {{
}}

void soc_{name_clean}_reset(void) {{
}}
""",
    }

    for path, content in files.items():
        if not dry_run:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        created_files.append(path)

    return created_files


def scaffold_board(name: str, soc_name: str, root: Path, dry_run: bool = False) -> list:
    name_clean = name.lower().replace("-", "_")
    name_upper = name_clean.upper()
    soc_clean = soc_name.lower().replace("-", "_")
    board_dir = root / "board" / name_clean
    created_files = []

    files = {
        board_dir / "CMakeLists.txt": f"""edge_add_board({name_clean}
    SOURCES
        src/board.c
    DEPS
        edge_module
        soc_{soc_clean}
)
""",
        board_dir / "include" / name_clean / "board.h": f"""#ifndef BOARD_{name_upper}_H
#define BOARD_{name_upper}_H

#include "edge/errors.h"
#include "edge/event.h"
#include <stdbool.h>
#include <stdint.h>

#define BOARD_{name_upper}_IRQ_MAX_HANDLERS 8u

typedef void (*board_{name_clean}_irq_fn)(void *ctx);

#ifdef __cplusplus
extern "C" {{
#endif

void board_{name_clean}_init(edge_event_sink_t *sink);
edge_status_t board_{name_clean}_irq_attach(uint32_t irq, board_{name_clean}_irq_fn cb, void *ctx);
void board_{name_clean}_irq_dispatch(uint32_t irq);
uint32_t board_{name_clean}_irq_handler_count(void);

/* Hardware lifecycle actions (D9/D71) */
void board_{name_clean}_enter_low_power(void);
void board_{name_clean}_feed_watchdog(void);
void board_{name_clean}_system_reset(void);

#ifdef __cplusplus
}}
#endif

#endif /* BOARD_{name_upper}_H */
""",
        board_dir / "src" / "board.c": f"""#include "{name_clean}/board.h"
#include "soc_{soc_clean}/soc_{soc_clean}.h"
#include <stddef.h>

typedef struct board_irq_slot {{
    uint32_t irq;
    board_{name_clean}_irq_fn cb;
    void *ctx;
}} board_irq_slot_t;

static edge_event_sink_t *g_sink = NULL;
static board_irq_slot_t g_handlers[BOARD_{name_upper}_IRQ_MAX_HANDLERS];
static uint32_t g_handler_count = 0u;

void board_{name_clean}_init(edge_event_sink_t *sink) {{
    g_sink = sink;
    soc_{soc_clean}_init();
}}

edge_status_t board_{name_clean}_irq_attach(uint32_t irq, board_{name_clean}_irq_fn cb, void *ctx) {{
    if (cb == NULL) {{
        return EDGE_EINVAL;
    }}
    if (g_handler_count >= BOARD_{name_upper}_IRQ_MAX_HANDLERS) {{
        return EDGE_ENOSPC;
    }}
    g_handlers[g_handler_count++] = (board_irq_slot_t){{
        .irq = irq,
        .cb = cb,
        .ctx = ctx,
    }};
    return EDGE_OK;
}}

void board_{name_clean}_irq_dispatch(uint32_t irq) {{
    for (uint32_t i = 0u; i < g_handler_count; ++i) {{
        if (g_handlers[i].irq == irq && g_handlers[i].cb != NULL) {{
            g_handlers[i].cb(g_handlers[i].ctx);
        }}
    }}
}}

uint32_t board_{name_clean}_irq_handler_count(void) {{
    return g_handler_count;
}}

void board_{name_clean}_enter_low_power(void) {{
}}

void board_{name_clean}_feed_watchdog(void) {{
}}

void board_{name_clean}_system_reset(void) {{
    soc_{soc_clean}_reset();
}}
""",
    }

    for path, content in files.items():
        if not dry_run:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        created_files.append(path)

    return created_files


def scaffold_product(name: str, board_name: str, family_name: str, app_name: str, root: Path, dry_run: bool = False) -> list:
    name_clean = name.lower().replace("-", "_")
    name_upper = name_clean.upper()
    board_clean = board_name.lower().replace("-", "_")
    family_clean = family_name.lower().replace("-", "_")
    app_clean = app_name.lower().replace("-", "_")
    product_dir = root / "product" / name_clean
    created_files = []

    files = {
        product_dir / "glue.h": f"""#ifndef PRODUCT_{name_upper}_GLUE_H
#define PRODUCT_{name_upper}_GLUE_H

#include "{app_clean}/ports.h"

#ifdef __cplusplus
extern "C" {{
#endif

void product_{name_clean}_make_sink({app_clean}_sink_if_t *out);

#ifdef __cplusplus
}}
#endif

#endif /* PRODUCT_{name_upper}_GLUE_H */
""",
        product_dir / "glue.c": f"""#include "glue.h"

static edge_status_t sink_emit(void *self, uint32_t val) {{
    (void)self;
    (void)val;
    return EDGE_OK;
}}

void product_{name_clean}_make_sink({app_clean}_sink_if_t *out) {{
    if (out == NULL) {{
        return;
    }}
    *out = ({app_clean}_sink_if_t){{
        .self = NULL,
        .emit = sink_emit,
    }};
}}
""",
        product_dir / "main.c": f"""#include "{app_clean}/{app_clean}.h"
#include "{board_clean}/board.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "{family_clean}/sys.h"
#include "glue.h"
#include <stdint.h>

static uint64_t monotonic_ticks(void *self) {{
    return ++(*(uint64_t *)self);
}}

int main(void) {{
    uint64_t clock_tick = 0u;
    {app_clean}_sink_if_t sink_port;
    {app_clean}_instance_t app_inst;
    {app_clean}_config_t app_cfg = {{.max_limit = 100u}};
    edge_module_t *apps[1];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {{
        .monotonic_ticks = monotonic_ticks,
        .wall_time = NULL,
        .self = &clock_tick,
    }};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;

    product_{name_clean}_make_sink(&sink_port);
    {app_clean}_construct(&app_inst, 1001u, 100u, &sink_port, &app_cfg);
    if ({app_clean}_init(&app_inst) != EDGE_OK) {{
        return 10;
    }}
    apps[0] = {app_clean}_module(&app_inst);

    if (edge_event_queue_init(&event_queue, event_storage, 16u) != EDGE_OK) {{
        return 1;
    }}
    event_sink = (edge_event_sink_t){{.queue = &event_queue, .clock = &clock, .guard = NULL}};
    board_{board_clean}_init(&event_sink);

    if (sys_{family_clean}_init(&sys, apps, 1u, &event_queue, subscriptions, 4u) != EDGE_OK) {{
        return 2;
    }}
    if (edge_sys_set_clock(&sys, &clock) != EDGE_OK) {{
        return 3;
    }}
    if (edge_sys_start(&sys) != EDGE_OK) {{
        return 4;
    }}

    for (unsigned i = 0u; i < 10u; ++i) {{
        if (edge_sys_run_once(&sys) != EDGE_OK) {{
            break;
        }}
    }}

    (void)edge_sys_power_off(&sys);
    (void){app_clean}_shutdown(&app_inst);
    (void)edge_sys_deinit(&sys);
    return 0;
}}
""",
    }

    for path, content in files.items():
        if not dry_run:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        created_files.append(path)

    return created_files


def main():
    parser = argparse.ArgumentParser(description="Modular architecture scaffolding engine")
    subparsers = parser.add_subparsers(dest="cmd", required=True)

    # app
    p_app = subparsers.add_parser("app", help="Scaffold a new Clean Architecture app")
    p_app.add_argument("name", help="Name of the app (e.g. telemetry)")
    p_app.add_argument("--root", default=".", help="Root repository path")
    p_app.add_argument("--dry-run", action="store_true", help="Print files without creating")

    # infra
    p_infra = subparsers.add_parser("infra", help="Scaffold a new infra driver")
    p_infra.add_argument("name", help="Name of the driver (e.g. spi_flash)")
    p_infra.add_argument("--root", default=".", help="Root repository path")
    p_infra.add_argument("--dry-run", action="store_true", help="Print files without creating")

    # soc
    p_soc = subparsers.add_parser("soc", help="Scaffold a new SoC support package")
    p_soc.add_argument("name", help="Name of the SoC (e.g. stm32g4)")
    p_soc.add_argument("--root", default=".", help="Root repository path")
    p_soc.add_argument("--dry-run", action="store_true", help="Print files without creating")

    # board
    p_board = subparsers.add_parser("board", help="Scaffold a new board layer")
    p_board.add_argument("name", help="Name of the board (e.g. nucleo_g474)")
    p_board.add_argument("--soc", required=True, help="SoC name this board binds to")
    p_board.add_argument("--root", default=".", help="Root repository path")
    p_board.add_argument("--dry-run", action="store_true", help="Print files without creating")

    # product
    p_prod = subparsers.add_parser("product", help="Scaffold a new product composition root")
    p_prod.add_argument("name", help="Name of the product (e.g. smart_switch)")
    p_prod.add_argument("--board", required=True, help="Board name this product binds to")
    p_prod.add_argument("--family", default="example", help="Sys scheduling family name (default: example)")
    p_prod.add_argument("--app", default="dlt645", help="Primary app name to construct (default: dlt645)")
    p_prod.add_argument("--root", default=".", help="Root repository path")
    p_prod.add_argument("--dry-run", action="store_true", help="Print files without creating")

    args = parser.parse_args()
    root = Path(args.root).resolve()

    if args.cmd == "app":
        created = scaffold_app(args.name, root, dry_run=args.dry_run)
        print(f"Scaffolded app '{args.name}' with {len(created)} files:")
        for f in created:
            print(f"  {f.relative_to(root)}")
    elif args.cmd == "infra":
        created = scaffold_infra(args.name, root, dry_run=args.dry_run)
        print(f"Scaffolded infra '{args.name}' with {len(created)} files:")
        for f in created:
            print(f"  {f.relative_to(root)}")
    elif args.cmd == "soc":
        created = scaffold_soc(args.name, root, dry_run=args.dry_run)
        print(f"Scaffolded soc '{args.name}' with {len(created)} files:")
        for f in created:
            print(f"  {f.relative_to(root)}")
    elif args.cmd == "board":
        created = scaffold_board(args.name, args.soc, root, dry_run=args.dry_run)
        print(f"Scaffolded board '{args.name}' with {len(created)} files:")
        for f in created:
            print(f"  {f.relative_to(root)}")
    elif args.cmd == "product":
        created = scaffold_product(args.name, args.board, args.family, args.app, root, dry_run=args.dry_run)
        print(f"Scaffolded product '{args.name}' with {len(created)} files:")
        for f in created:
            print(f"  {f.relative_to(root)}")
        print("\nNext step: bind this product in top-level CMakeLists.txt:")
        print(f"  edge_add_product({args.name} family {args.family} board {args.board} apps {args.app})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
