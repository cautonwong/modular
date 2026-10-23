# Optional Module mechanism: source study, design, build system

**Status: proposal, not frozen.** This answers what #241 left open and specifies the
build system that would implement it. No ADR row is added here: D89-D92 are
*reserved* for it and land in [`adr.md`](adr.md) only when a maintainer signs the
level model off. Nothing in the tree changes behaviour today; Level 0 stays the
only level that exists.

Section 2 is a source study of Eclipse ThreadX Modules
(`eclipse-threadx/threadx`, local tree `/workspaces/rtos_bak/threadx`) with
file/line citations, because every decision in §4 is justified against what that
code actually does - not against the marketing summary.

---

## 1. What is true in this tree today (measured)

| Fact | Value / evidence |
|---|---|
| Highest decision id | `D88` - D89-D92 free |
| `edge_module_t` | 56 B on ARM32/RV32, 88 B on LP64 (`_Static_assert` in `edge/module.h`) |
| `edge_module_desc_t` | id, name, instance_size, deps, lifecycle hooks. **No ABI version, no private-RAM need** (`edge/desc.h`) |
| Failed-module isolation | implemented: `sys.c` marks `failed`, drops it from poll and dispatch, counts `stats.isolated` (D53) |
| Slot-capable linker script | `.github/arm/cortex-m4-qemu.ld`: FLASH 64 KiB @0x0, RAM 16 KiB @0x20000000 |
| Firmware headroom (QEMU demo target) | `meter_mps2_fw`: **text 6728 B, data 0, bss 100 B** of 64 KiB / 16 KiB |
| MPU code | none outside vendor trees → Level 3 starts from zero |
| Separate-image build support | none: no `edge_add_module_image()`, no preamble generator, no objcopy/embed path |
| Measurement toolchain | `arm-none-eabi-gcc`, `qemu-system-arm` both present |

Consequence: Level 1 is a small extension of something already working, Level 2 is
new machinery on a comfortable budget, Level 3 is a platform project.

---

## 2. Source study: what ThreadX Modules actually does

### 2.1 The preamble is a hand-written word array, with offsets relative to itself

`common_modules/inc/txm_module.h` (preamble struct, ~line 300) and the module-side
`ports_module/cortex_m7/gnu/example_build/txm_module_preamble.S`:

```
id (0x4D4F4455), version_major, version_minor, preamble_size_words,
application_module_id, property_flags,
shell_entry_function, start_function, stop_function,
start_stop_priority, start_stop_stack_size,
callback_function, callback_priority, callback_stack_size,
code_size, data_size, reserved_0..14, checksum
```

Three details that matter for our design:

1. **The entry fields hold offsets, not addresses.** The assembly writes
   `_txm_module_thread_shell_entry - . - 0`, i.e. relative to the field's own
   position, and the loader resolves them as
   `code_start + preamble_field + compiler_adjust`
   (`txm_module_manager_internal_load.c`, ~line 340).
2. **A per-compiler adjust hack exists** because IAR/RVDS/GNU place the entry
   differently: `TXM_MODULE_GNU_SHELL_ADJUST 24`, `..._START_ADJUST 28`,
   `..._STOP_ADJUST 32`, `..._CALLBACK_ADJUST 44`
   (`ports_arch/ARMv7-M/threadx_modules/inc/txm_module_port.h`, ~line 205). It is a
   portability tax we can avoid entirely by generating the header from the ELF.
3. **The property word mixes compiler identity, options and alignment**:
   `TXM_MODULE_OPTIONS_MASK 0x000000FF`, `TXM_MODULE_COMPILER_MASK 0xFF000000`,
   the sample writes `0x02000007` = GNU | USER_MODE|MEMORY_PROTECTION|
   SHARED_EXTERNAL_MEMORY_ACCESS.

### 2.2 `in_place_load` keeps the code and still allocates data per instance

`txm_module_manager_in_place_load.c` (114 lines) does only: pick up `code_size`
(reject 0), check code alignment, then call `_txm_module_manager_internal_load(instance,
name, location, code_size, TX_NULL, 0)`.

`internal_load.c` (418 lines) is where the real work is:

- validates `property_flags & TXM_MODULE_OPTIONS_MASK` against
  `_txm_module_manager_properties_supported` / `_required`, and rejects
  `MEMORY_PROTECTION` without `USER_MODE` (~line 156);
- **allocates the data area from `_txm_module_manager_byte_pool`** - a byte pool
  (heap) created from caller-supplied memory in
  `txm_module_manager_initialize.c` (~line 169) - and `TX_MEMSET`s it to zero
  (~line 280);
- lays the allocation out as `[start/stop thread stack][callback thread stack][module
  data area]` (comment block in the same function);
- records `data_start/data_end/data_allocation_ptr`, resolves the entry pointers,
  and links the instance into a **circular loaded list** - so the same in-place
  image can be loaded several times with separate data areas.

`memory_load.c` additionally allocates *and copies* the code (byte pool, ~line 112)
before calling the same internal loader.

### 2.3 Data is addressed through `r9`, and the loader patches a GOT

- `ports_arch/ARMv7-M/threadx_modules/gnu/module_manager/src/txm_module_manager_thread_stack_build.s`
  line ~114: "Pickup data base register from the module information" →
  `STR r3, [r2, #24]` stores the initial `r9` for the module thread.
- The module is compiled with
  `-fpie -fno-plt -mno-pic-data-is-text-relative -msingle-pic-base`
  (`ports_module/cortex_m7/gnu/example_build/build_threadx_module_sample.bat`), so
  all static data references go through `r9` + GOT.
- `_gcc_setup` (module-side, `gcc_setup.s`) copies the GOT into the per-instance
  data area and rebases each entry: code addresses relative to the code base,
  data addresses relative to the `r9` data base.
- The module linker script therefore has `.preamble`, `.dynsym`, `.dynstr`, `.got`,
  `.new_got`, code and data sections (`sample_threadx_module.ld`).

So ThreadX in-place modules are *not* plain static binaries: they carry a GOT, a
`r9` discipline, and a loader-side relocation step.

### 2.4 Module → kernel calls go through a function-pointer dispatcher

- `txm_module_thread_shell_entry.c` (~line 44, 111) keeps
  `ULONG (*_txm_module_kernel_call_dispatcher)(ULONG req, ULONG p1, ULONG p2, ULONG p3)`
  and fills it from `thread_info->txm_module_thread_entry_info_kernel_call_dispatcher`,
  spinning until valid. Every `txm_*` API stub calls through it.
- In the module build that pointer is delivered by a trap: the module-manager port's
  `SVC_Handler` (`ports_arch/ARMv7-M/threadx_modules/gnu/module_manager/src/tx_thread_schedule.S`,
  ~line 464) distinguishes **SVC 1 = entering ThreadX from user mode**, verifies the
  caller's LR against `_txm_module_priv`, switches to the thread's *kernel* stack
  (`tx_thread_module_kernel_stack_start/end/size` in the thread extension,
  `txm_module_port.h` ~line 380), and only then runs `_txm_module_manager_kernel_dispatch`.
- `txm_module_manager_kernel_dispatch.c` is a 940-line `switch` with **97**
  `#ifndef TXM_<SERVICE>_CALL_NOT_USED` guards: the service whitelist is
  compile-time, per build, not a runtime table.

That is the single most useful thing to copy conceptually: **a module never links a
resident symbol; it reaches the core through one dispatcher and a request id.**
Our Level 2 gets the same property with a plain function-pointer table and no SVC.

### 2.5 Lifecycle: start creates two kernel threads with their own stacks

`txm_module_manager_start.c` (~line 100): requires state LOADED or STOPPED, rejects
`start_stop_priority`/`callback_priority` below `maximum_priority`, creates a
callback request **queue**, then creates *two* threads via
`_txm_module_manager_thread_create` with `TX_DONT_START` - "Module Start Thread" and
"Module Callback Request Thread" - each with a module-declared stack size and
priority, each thread carrying a kernel stack for privileged execution.
`stop`/`stop_extended` runs the stop thread; `unload.c` frees the data allocation
from the byte pool and unlinks the instance; `object_allocate.c` hands the module
memory for kernel objects (queues/semaphores/timers) out of an object pool, tracked
per instance so unload can reclaim them.

### 2.6 Memory protection

Three property bits (`txm_module_port.h` ~line 205): `USER_MODE 0x1`,
`MEMORY_PROTECTION 0x2`, `SHARED_EXTERNAL_MEMORY_ACCESS 0x4`.
MPU region setup lives in the port
(`ports_arch/ARMv7-M/threadx_modules/common/module_manager/src/txm_module_manager_mm_register_setup.c`)
with per-region access-control constants `TXM_MODULE_MPU_CODE_ACCESS_CONTROL
0x06070000`, `..._DATA_ACCESS_CONTROL 0x13070000`, and
`txm_module_manager_external_memory_enable()` adding a declared shared region.
Threads run unprivileged; ISRs must stay in the manager because interrupt entry
escalates privilege.

### 2.7 Integrity: the checksum field is never checked

`txm_module_preamble_checksum` is documented "[Optional]", and `grep` over
`common_modules/module_manager/src/` finds **no** verification code. ThreadX does not
validate the image it loads. So our D90 CRC is an addition, not a port - and it is
the cheapest real improvement available, because a slot in flash is exactly the
thing an attacker rewrites first (`docs/threat-model.md`, T1).

### 2.8 What each ThreadX feature costs

| Feature | What it costs in ThreadX | Fits this tree? |
|---|---|---|
| per-instance data area | a byte-pool heap in the module manager | **no** - D21 |
| `r9` + GOT + `_gcc_setup` relocation | image must be built `-fpie -msingle-pic-base`, loader patches the GOT | **no** - we want a plain static image |
| two kernel threads per module + kernel stacks | scheduler objects, stack budget, priority policy | **no** - D47 single runner |
| SVC dispatcher + user mode | privilege transition, MPU regions, kernel stack switch | **later** - Level 3, needs an MPU |
| offset fields + compiler adjust | hand-written per-compiler preamble assembly | **no** - generate from the ELF |
| checksum | not implemented | **yes** - implement it |
| service dispatch by request id | 97 compile-time `*_CALL_NOT_USED` guards | **yes**, as the Level 3 service gate |

---

## 3. Why we do not port the mechanism

Six concrete costs from §2, each colliding with a frozen decision: byte-pool heap
(D21), module threads (D47), PIC/GOT build discipline (D40 simplicity, and the core
must stay link-time honest), per-compiler preamble assembly (D7: CMake + `main()`
are the truth), privilege/SVC/MPU (needs hardware support we do not have wired),
and runtime discovery (D5). What survives the filter is the *shape*: a validated
header at a fixed address, an explicit bind, one dispatcher for core access, and a
register/unregister step against `sys`.

---

## 4. The model we do build

### 4.1 Levels

| Level | Name | Adds | Shipping state |
|---|---|---|---|
| 0 | Static (today) | `main()` composes modules by hand | only level that exists |
| 1 | Logical module | descriptor carries ABI + private-RAM need; fault isolation already there | Phase A |
| 2 | In-place slot image | separately linked image at a fixed slot + preamble + host bind/start/stop/unload | Phase B |
| 3 | Arena + optional MPU | per-instance arena carve, MPU regions, service gate | Phase C (gate) / D (MPU) |

### 4.2 Decision A - images import through a table passed at bind

```c
/* edge_module/include/edge/module_host.h */
#define EDGE_MODULE_IMAGE_ABI_MAJOR 1u
#define EDGE_MODULE_IMAGE_ABI_MINOR 0u

typedef struct edge_module_imports {
    uint16_t abi_major;
    uint16_t abi_minor;
    void *host;                                   /* opaque, returned to the entry */
    void (*push_event)(void *host, const edge_event_t *event);
    uint64_t (*now)(void *host);
    void (*log)(void *host, uint32_t level, const char *text);
    /* append-only (D40): new imports go at the end */
} edge_module_imports_t;

/* The image's only exported symbol. */
edge_status_t edge_module_image_entry(const edge_module_imports_t *imports,
                                     void *arena, size_t arena_size,
                                     const edge_module_t ***modules,
                                     size_t *module_count);
```

Rejected alternatives, with the reason recorded: linker-baked absolute addresses (a
core rebuild silently breaks every shipped image - the opposite of the point), and
SVC-first (meaningless without privilege modes; deferred to Level 3 where
`edge_service_call()` reuses the same table shape).

### 4.3 Decision B - the image is code + rodata only; all mutable state is the arena

This is the largest simplification versus ThreadX and it follows directly from D21:

- The image has `.text` + `.rodata` and **no `.data`, no `.bss`, no `.got`**.
  Build gate fails an image whose `.data`/`.bss` are non-empty.
- Everything mutable - the `edge_module_t` itself, counters, buffers - lives in an
  arena slice the host passes to `edge_module_image_entry()`. The host zeroes the
  slice once at bind (it is a `.bss` array in the firmware, already zero at reset;
  zeroing again makes unload/rebind deterministic).
- No GOT, no relocation, no `r9` discipline, no PIC flags: the image is a normal
  static binary linked at its slot address. `-fpie` is *not* used.
- Multi-instance is therefore structurally possible later (two arena slices) but is
  not wired in Level 2: `sys` assumes one `module_id` per running app and the demo
  does not need it. Recorded as a deliberate non-feature, not a limitation of the
  mechanism.

### 4.4 Decision C - module ids stay in the app block

`edge/modules.h` reserves `0x9000-0xFFFF` as non-allocatable and
`check_module_ids.py` rejects out-of-block values. An image *is* an app (D73), so
its modules keep app-block ids; `slot_index` in the preamble is the image's storage
identity and nothing else. No id-table change. (Answers #241 open question 4.)

### 4.5 Decision D - the preamble is generated from the ELF, and the CRC is checked

```c
/* edge_module/include/edge/module_image.h */
typedef struct edge_module_image_hdr {
    uint32_t magic;            /* 'EDGM' = 0x4D474445 */
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;            /* bit0 IN_PLACE, bit1 RESERVED(was NEEDS_MPU), bit2 ALLOW_SHARED */
    uint32_t module_id;        /* app-block id of the primary module */
    uint32_t image_crc;        /* CRC-32 over .text + .rodata, as linked */
    uint32_t text_size;        /* .text + .rodata bytes */
    uint32_t entry_offset;     /* edge_module_image_entry - slot_origin */
    uint32_t arena_size;       /* private RAM the image needs */
    uint32_t slot_index;
    uint32_t reserved[3];      /* append-only */
} edge_module_image_hdr_t;
```

Generated by `tools/gen_module_image_hdr.py` from the linked ELF (see §6.5), so:
no hand-written assembly, no per-compiler adjust constants, and a CRC that cannot
disagree with the bytes it describes. Bind order is fixed: magic → ABI major →
flags → CRC → sizes → arena budget; any failure is a rejected bind with a distinct
status, never a partial start.

### 4.6 Decision E - the host lives in `sys/module_host/`

Its own area target (D88), shared by every product family rather than per-family
(answers #241 open question 2). It owns no device knowledge: CRC is a small
table-free implementation, the slot table comes from the product.

### 4.7 Host API and state machine

```c
typedef enum {
    EDGE_MODULE_INSTANCE_EMPTY = 0,
    EDGE_MODULE_INSTANCE_BOUND,
    EDGE_MODULE_INSTANCE_STARTED,
    EDGE_MODULE_INSTANCE_STOPPED,
    EDGE_MODULE_INSTANCE_FAULTED,
} edge_module_instance_state_t;

typedef struct edge_module_instance {
    const edge_module_image_hdr_t *hdr;   /* points into the slot */
    const edge_module_t **modules;
    size_t module_count;
    void *arena;
    size_t arena_size;
    edge_module_instance_state_t state;
} edge_module_instance_t;

edge_status_t edge_module_host_init(void *arena, size_t arena_size);
edge_status_t edge_module_host_bind(edge_module_instance_t *inst, const void *image_base,
                                    size_t slot_size, const edge_module_imports_t *imports);
edge_status_t edge_module_host_start(edge_module_instance_t *inst, edge_sys_t *sys);
edge_status_t edge_module_host_stop(edge_module_instance_t *inst, edge_sys_t *sys);
edge_status_t edge_module_host_unload(edge_module_instance_t *inst);
```

Transition rules (each one a unit test):

| From | Call | To | Failure leaves state |
|---|---|---|---|
| EMPTY | `bind` | BOUND | EMPTY |
| BOUND | `start` | STARTED | BOUND (registration rolled back) |
| STARTED | `stop` | STOPPED | STARTED |
| BOUND/STOPPED | `unload` | EMPTY | unchanged |
| any | module faulted at runtime | FAULTED | FAULTED (no restart in Level 2) |

`start` calls each module's `init` (D51: assembly-time work belongs to the caller),
then `edge_sys_add`-registers it through the existing explicit path; `stop` calls
`sys` removal then `deinit`; `unload` only clears the instance and re-zeroes the
arena. Nothing is discovered: the product names the slot.

---

## 5. Level by level: implementation spec

### Level 1 (Phase A, ~3-4 h)

| File | Change |
|---|---|
| `edge_module/include/edge/desc.h` | append `uint16_t abi_version; uint16_t reserved; size_t private_size;`; add `edge_module_desc_validate()` |
| `edge_module/include/edge/module.h` | no change (desc is a separate struct) |
| `app/dlt645/src/dlt645.c`, `app/relay/src/relay.c` | fill the new fields |
| `tests/test_module_desc.c` | ABI mismatch, zero `private_size`, oversized `private_size` |
| `docs/module-levels.md` | this file |
| `docs/adr-conformance.md` | D89 row (🟡) once the ADR lands |

### Level 2 (Phase B, ~2-3 days)

| Piece | Where |
|---|---|
| preamble + import table + host API | `edge_module/include/edge/module_image.h`, `edge_module/include/edge/module_host.h`, `sys/module_host/` |
| CRC-32 (table-free, ~20 lines) | `sys/module_host/src/crc32.c` |
| slot linker script template | `cmake/module_slot.ld.in` |
| image build function | `cmake/EdgeModuleImage.cmake` |
| header generator | `tools/gen_module_image_hdr.py` |
| demo image (composes existing `app/relay`) | `product/meter_slot_relay/{entry.c,glue.c}` |
| demo firmware | `product/meter_mps2_slot/main.c` + `edge_add_arm_firmware(...)` |
| guards | `check_module_image.py`, `check_module_image_symbols.py`, slot budget in `ci/size-budget.json` |
| tests | `tests/test_module_host.c` (host, synthetic image), `tests/guards/module_image_*` |

### Level 3a - service gate (Phase C, ~1-2 days, no MPU needed)

`edge_svc_id_t` whitelist, module-side stubs that call through the import table, and
`check_module_image_symbols.py` proving the image's undefined set is exactly the
stub set. Runnable on the current board; it makes the Level 2 contract enforceable
rather than aspirational. Mirrors ThreadX's 97 compile-time `*_CALL_NOT_USED`
guards, but as one generated table instead of 97 `#ifndef`s.

### Level 3b - arena + MPU (Phase D, 2+ weeks)

Bump/slab carve per instance over a product arena, MPU regions programmed by
`pal`/`board` (`TXM_MODULE_MPU_*_ACCESS_CONTROL` equivalents),
`external_memory_enable`-style declare-only shared regions, and a memory-fault path
that marks the instance FAULTED. Requires MPU support in `soc/mps2` (none today)
**and** a second board, otherwise it stays a one-board special.

---

## 6. Build system design

This is the part #241 hand-waved. It is the difference between "a design" and
"three days of work", so it is specified at the level of CMake function signatures,
flags, and generated files.

### 6.1 Target graph

```
app/relay  --(static lib, unchanged)--> meter_slot_relay_image   [ELF at slot address]
                                              |  objcopy -O binary
                                              |  gen_module_image_hdr.py
                                              v
                                      slot0_image.c (generated)  [byte array + hdr]
                                              |
product/meter_mps2_slot/main.c ---------------+
                                              v
                                  meter_mps2_slot_fw   [linked with slot0 at 0x9000]
```

Two rules keep that graph honest:

- the image target **never links** `edge_module`, `sys`, `board`, `infra`, `pal` or
  any resident code - only the app being packaged;
- the firmware target links the image only as *data*, never as code: no symbol from
  the image is callable, and `check_module_image_symbols.py` proves it in both
  directions.

### 6.2 CMake surface

```cmake
# cmake/EdgeModuleImage.cmake
#
# edge_add_module_image(<name> PRODUCT <product_dir> MODULE <primary_module_id>
#                       SLOT <index> ARENA <bytes>)
#
# Builds a separately linked image for one app composition, at the address of
# slot <index> from the firmware's slot map, generates its preamble from the
# linked ELF, and exposes it as target property EDGE_MODULE_IMAGE_<name>.
function(edge_add_module_image name)
```

Product side (firmware):

```cmake
# cmake/EdgeTargets.cmake, next to edge_add_arm_firmware()
#
# edge_add_arm_firmware(... ) gains one optional argument:
#   SLOTS "SLOT0=0x9000:8192"
edge_add_arm_firmware(meter_mps2_slot_fw meter_mps2_slot cortex-m4 \
                      .github/arm/cortex-m4-qemu-slot.ld board_mps2_irq_timer0
                      SLOTS "SLOT0=0x9000:8192")
edge_add_module_image(meter_slot_relay
                      PRODUCT product/meter_slot_relay
                      MODULE ${EDGE_MOD_RELAY}
                      SLOT 0
                      ARENA 1024)
```

The two calls meet in a generated header the firmware compiles:
`slot_map.h` with `EDGE_SLOT_COUNT`, `EDGE_SLOT0_ORIGIN`, `EDGE_SLOT0_SIZE`, and
`edge_slot0_image()` / `edge_slot0_image_size()`. Generated by CMake, not typed by
hand, so the addresses in the linker script, the image link and the bind call
cannot disagree.

### 6.3 Compile flags per stage

| Stage | Flags | Why |
|---|---|---|
| image object code | `-mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections -fno-pic -fno-pie -fno-common -fno-stack-protector` | plain static; **no** `-fpie`/`-msingle-pic-base`, because there is no GOT to patch (contrast §2.3) |
| image link | `-T cmake/module_slot.ld.in→build/module_slot0.ld --defsym SLOT_ORIGIN=--entry edge_module_image_entry -nostdlib -Wl,--gc-sections` | fixed address, no CRT, no libc |
| firmware link | existing `edge_add_arm_firmware` flags + `SLOTS` | unchanged otherwise |

`-nostdlib` is deliberate: an image that pulled in `printf` would violate D21 and
blow the slot budget silently. The compiler-helpers allowlist for the symbol guard
is therefore `{}` at Level 2 (`memcpy`/`memset` appear only if the app calls them -
and the guard fails, which is the point: the image asks the host for the service or
does it inline). If a real app needs them, the allowlist gains exactly four names
and the decision is recorded in the guard's config, not in a comment.

### 6.4 Slot map and linker script

`cmake/module_slot.ld.in`, configured per slot with `SLOT_ORIGIN`/`SLOT_SIZE`:

```ld
ENTRY(edge_module_image_entry)
SECTIONS
{
    .text SLOT_ORIGIN :
    {
        KEEP(*(.text.edge_module_image_entry))
        *(.text*) *(.rodata*)
        . = ALIGN(4);
        _image_end = .;
    }
    /DISCARD/ : { *(.data*) *(.bss*) *(.got*) *(.comment) *(.note*) *(.ARM.attributes) *(.ARM.exidx*) }
    ASSERT(_image_end - SLOT_ORIGIN <= SLOT_SIZE, "module image exceeds its slot")
    ASSERT(SIZEOF(.data) == 0, "module image must not carry mutable state (arena instead)")
}
```

The firmware's own `cortex-m4-qemu-slot.ld` reserves the region and pins the byte
array into it:

```ld
.slot0 SLOT0_ORIGIN : { KEEP(*(.edge_slot0)) . = ALIGN(4); } > FLASH
ASSERT(ADDR(.slot0) == SLOT0_ORIGIN, "slot0 placed at the wrong address")
ASSERT(SIZEOF(.slot0) <= SLOT0_SIZE, "slot0 image does not fit")
```

The generated array carries `__attribute__((section(".edge_slot0"), used, aligned(8)))`,
and the *first* bytes of the binary are the preamble by construction (§6.6), so the
image's own layout and where the firmware puts it are the same question, checked by
two `ASSERT`s and one guard.

### 6.5 `tools/gen_module_image_hdr.py`

Inputs: the linked image ELF, slot index, primary module id, slot origin.
Outputs: `slot0_image.c` containing the byte array, the `edge_module_image_hdr_t`
instance, and the accessors. Steps, all deterministic:

1. `readelf -S/-l` (or `pyelftools`-free `arm-none-eabi-readelf -x` parsing) to get
   `.text` + `.rodata` contents and the entry symbol address;
2. pad the preamble to a 16-byte boundary, then concatenate
   `[hdr][.text][.rodata]`, computing `text_size` from the two sections;
3. `image_crc = crc32(payload)` over exactly the concatenated bytes that follow the
   header (not over the header itself - a self-describing header cannot cover
   itself);
4. `entry_offset = &edge_module_image_entry - SLOT_ORIGIN` (thumb bit cleared; the
   loader ORs it back in, which is the one ThreadX detail worth keeping);
5. emit the C file with `const` array, `_Static_assert(sizeof(hdr) == 48)`, and the
   accessors.

The simulator/host path reuses the same tool with `--origin 0 --host`, so the unit
test and the firmware agree byte for byte.

### 6.6 Why a generated C array and not `objcopy -I binary`

`objcopy -I binary` produces an object whose section has no `SHF_ALLOC`/alignment
guarantees we control, and its arch flags (`-B arm`) are a foot-gun when the same
project also builds RISC-V. A generated C array is boring, portable across the
toolchains in `.github/`, and carries its own alignment and section attributes.
Cost: one extra translation unit of ~8 KiB of hex; it compiles as data, not code.

Reproducibility is already handled by the tree: `SOURCE_DATE_EPOCH` is set in CI
(`embedded-smoke.yml`), and the generator must not embed a timestamp or host path -
checked by the existing "reproducible firmware check" step, which now also covers
the slot bytes.

### 6.7 Firmware-side wiring

```c
/* product/meter_mps2_slot/main.c (sketch) */
static uint8_t g_arena[EDGE_SLOT_COUNT][EDGE_SLOT0_SIZE / 8];
static edge_module_instance_t g_slot0;

int main(void) {
    board_mps2_init(&sink);
    sys_meter_init(&sys, /* existing */);
    edge_module_host_init(g_arena, sizeof g_arena);

    const edge_module_imports_t imports = {
        .abi_major = EDGE_MODULE_IMAGE_ABI_MAJOR, .abi_minor = EDGE_MODULE_IMAGE_ABI_MINOR,
        .host = &sys, .push_event = host_push_event, .now = host_now, .log = host_log,
    };
    if (edge_module_host_bind(&g_slot0, edge_slot0_image(), edge_slot0_image_size(), &imports) != EDGE_OK)
        return 20;                        /* distinct exit code, inspectable in QEMU */
    if (edge_module_host_start(&g_slot0, &sys) != EDGE_OK)
        return 21;
    edge_sys_run_once(&sys);              /* existing runner: slot modules are just modules */
    return 0;
}
```

Exit codes `20`/`21` join the existing encoding in `docs/rtos-runner.md` §6, so the
QEMU smoke asserts them the same way it asserts `0` today.

### 6.8 Host-testable too

`tests/test_module_host.c` builds a **host image** target: the same generator, a
host linker script at origin 0, and a fake `edge_module_image_entry` that returns a
synthetic module. That gives the state machine, CRC rejection, ABI rejection, arena
overflow and rollback real unit tests in `ctest` - no ARM, no QEMU. This is what
keeps Phase B to days instead of a bring-up.

### 6.9 Guards and fixtures

| Guard | Rule | Fixtures |
|---|---|---|
| `check_module_image.py` | linked VMA == slot origin; `.data`/`.bss` empty; hdr magic/ABI/CRC match the bytes; `entry_offset` inside the image; `text_size` == sum of sections | `module_image_good`, `module_image_bad_crc`, `module_image_bad_data`, `module_image_bad_origin` |
| `check_module_image_symbols.py` | undefined symbols ⊆ allowlist; no resident symbols in the image's `.symtab` | `module_image_good_symbols`, `module_image_bad_symbol` |
| `check_map_budget.py` | new `slot` layer budget (8192 flash / 1024 ram) | extend `map_budget_*` |
| `check_slot_map.py` | generated `slot_map.h` agrees with the linker script and the `SLOTS` property | `slot_map_good/bad` |

All four register in `run_guard_selftest.py`; `check_guard_coverage.py` already
forces a positive *and* negative fixture for each.

### 6.10 What the build system deliberately does not do

No relocation, no `.dynsym`, no GOT, no per-instance data copy, no module-side
libc, no threads, no discovery of images by section name (the product names the
slot), and no runtime "load from filesystem" - that is Level 3 plus #26.

---

## 7. Test matrix

| Layer | Where | Asserts |
|---|---|---|
| host unit | `tests/test_module_host.c` | state machine, CRC/ABI/magic rejection, arena budget, rollback |
| guard fixtures | `tests/guards/module_image_*` | each guard fails on its violation |
| QEMU smoke | `embedded-smoke` job, `-M mps2-an386` + semihosting | exit `0` = bound, started, relay polled; `20`/`21` reserved for bind/start failure (10-16 already mean something else, `docs/rtos-runner.md` §6) |
| size | `check_size.py`, `check_map_budget.py` | slot cannot grow into core headroom |
| parity | `check_runner_parity.py` (#242) | a slot module's events appear in the observation log like any other module's |

---

## 8. Phases, with file-level checklists and estimates

| Phase | Deliverable | Estimate |
|---|---|---|
| A | desc fields + validate + 3 test cases + conformance row | 3-4 h |
| B1 | `module_image.h`, `module_host.h`, `sys/module_host/` + host tests | ~1 day |
| B2 | `EdgeModuleImage.cmake`, `module_slot.ld.in`, `gen_module_image_hdr.py`, host image target | ~1 day |
| B3 | `meter_slot_relay` image + `meter_mps2_slot` firmware + QEMU exit codes | ~half day |
| B4 | 4 guards + fixtures + CI wiring | ~half day |
| C | service gate + symbol whitelist | 1-2 days |
| D | arena carve + MPU + fault path | 2+ weeks |

Total for Level 1 + Level 2: **~3 working days**, matching the earlier estimate now
that the build system is specified rather than assumed.

---

## 9. Risks and open items

1. **Local QEMU does not reproduce the CI smoke.** `qemu-system-arm -M mps2-an386
   -semihosting-config enable=on,target=native -kernel meter_mps2_fw` was killed at
   10-30 s here (`rc=137`) while CI asserts `0`. Phase B's smoke verification
   therefore relies on CI until this is understood; it does not block B1/B2, which
   are host-tested.
2. **Slot vs map budget.** 8 KiB is comfortable today (core text 6728 B), but the
   FreeRTOS/EOS variants budget 16 KiB flash for the kernel alone; a slot on those
   products needs its own map arithmetic, which is why the demo is the bare-metal
   `meter_mps2_slot`.
3. **Arena lifetime.** `edge_product_shutdown` must stop the instance before the
   arena's stack frame dies; the demo keeps both `static`, and a product that does
   not is a review item (same class of bug as `product/meter_host/main.c`'s
   `required[]` comment).
4. **`.data`-free rule can be painful.** An app with a lookup table in `.data`
   (const-initialized, non-const variable) fails the build. Fix in the app, not in
   the loader: make it `const` (→ `.rodata`) or move it to the arena.
5. **Volatile/`const` in flash read by an untrusted module** is the Level 3 threat,
   not a Level 2 one; `threat-model.md` T2 owns it.

## 10. Open questions for maintainers (3)

1. `D89` wording: is Level 1 "a descriptor extension" or "a module model"? It changes
   whether one conformance row covers D89 or D89-D91 together.
2. Is a Level 2 image inside the D45 core file list? (`threat-model.md` §7 asks the
   same; #241 cannot answer it alone.)
3. Is 8 KiB the reference slot size, or should it be sized to the largest shipped
   app (`app/dlt645`) so the demo cannot pass with a slot only it needs?

## 11. Related

- #241 (design), #242 (runner parity - a slot module's events belong in the same
  observation schema), #243 (`threat-model.md` deny list, T1/T2), #93 MPU, #95 SVC,
  #26 OTA delivery, #136/#29 certification boundary
- Decisions: D5, D7, D14, D15, D21, D40, D45, D47, D50, D73, D76, D85, D88
- Upstream studied: `eclipse-threadx/threadx` `common_modules/`, `ports_arch/ARMv7-M/threadx_modules/`,
  `ports_module/cortex_m7/gnu/example_build/` (citations inline in §2)
