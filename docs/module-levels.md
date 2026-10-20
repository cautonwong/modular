# Optional Module mechanism: levels, decisions, phases

**Status: proposal, not frozen.** This file answers the questions #241 left open so
the work can be split and estimated. It adds no ADR row yet: D89-D92 are
*reserved* here and land in [`adr.md`](adr.md) only when a maintainer signs them
off. Nothing in the tree changes behaviour because of this document; Level 0 stays
the only level that exists.

## 1. What is true in the tree today (measured, not assumed)

| Fact | Value / evidence |
|---|---|
| Highest decision id | `D88` in `adr.md` - D89-D92 are free |
| `edge_module_t` | 56 B on ARM32 / RV32, 88 B on LP64; `_Static_assert`s in `edge/module.h` |
| `edge_module_desc_t` | exists (`edge/desc.h`): id, name, instance_size, deps, lifecycle hooks. **No ABI version, no private-RAM need** |
| Failed-module isolation | implemented: `sys.c` marks `failed`, drops it from poll and dispatch, counts `stats.isolated` (D53) |
| Slot-capable linker script | yes: `.github/arm/cortex-m4-qemu.ld` (FLASH 64 KiB @0x0, RAM 16 KiB @0x20000000) |
| Firmware headroom on the QEMU demo target | `meter_mps2_fw`: **text 6728 B, data 0, bss 100 B** of 64 KiB / 16 KiB → an 8 KiB slot + 2 KiB private RAM fits with ~7x margin on the flash figure |
| MPU code in the tree | none outside vendor trees (`pal/rtos/chibios/vendor`, a header in `pal/eos`) → Level 3 starts from zero |
| Separate-image build support | none: no `edge_add_module_image()`, no objcopy-embed path, no preamble |

So Level 1 is a small extension of something that already works, Level 2 is new
machinery on a comfortable budget, and Level 3 is a platform project.

## 2. ThreadX Modules, verified, and what we deliberately drop

Checked against `eclipse-threadx/rtos-docs` (threadx-modules ch.1/5, appendix) and
`common_modules/inc/txm_module.h`:

| ThreadX | Mechanism confirmed | Our mapping (#241) |
|---|---|---|
| preamble | `txm_module_preamble_id` (`0x4D4F4455`), major/minor version, preamble size in 32-bit words, module id, entry/callback entries, stack size/priority, then memory sizes | `edge_module_image_hdr_t`, same idea, smaller (no threads) |
| `in_place_load` / `absolute_load` | code+data at fixed addresses, **no relocation, position independence not needed** | exactly Level 2: fixed slot, no PIE |
| `memory_load` | relocatable image | **dropped, permanently** - needs PIE and a relocator; the slot model already buys the product outcome |
| module properties | `TXM_MODULE_USER_MODE 0x1`, `TXM_MODULE_MEMORY_PROTECTION 0x2`, `TXM_MODULE_SHARED_EXTERNAL_MEMORY_ACCESS 0x4` | Level 3 only; flags word reserved in our header from Level 2 |
| kernel access | module → SVC → request IDs (`TXM_APPLICATION_REQUEST_ID_BASE 0x10000`) | Level 3 (`edge_service_call`); Level 2 uses an import table instead (§3) |
| `external_memory_enable` | programs MPU/MMU table for a declared region | Level 3; Level 2 declares nothing |
| ISRs | must live in manager (resident) space | already true here (D85); stated as a deny rule in `threat-model.md` |
| module threads | one thread per module, callback thread, priorities | **dropped by default** - conflicts with single-runner D47. A module is 1..n `edge_module_t`, polled by the resident runner (D73) |

Conclusion: the mapping in #241 is factually right, and the pieces we refuse are
exactly the ones that would break D5/D14/D21/D47/D50.

## 3. The four decisions this proposal makes

### A. Level 2 images import through a table, not through linker fixups

The open question #241 did not answer: how does a separately linked image call
resident code? Three options, and why the table wins:

| Option | Verdict |
|---|---|
| Absolute symbol addresses baked into the image (linker `--defsym`) | **rejected**: the image then hard-codes resident addresses, so any core rebuild that moves a function silently breaks every shipped slot image - the opposite of "upgrade the app without rebuilding the certified core" |
| SVC + request-id dispatch from day one | **deferred to Phase C**: needs privilege modes and an MPU to be meaningful; without them it is a jump table with extra assembly |
| **Function-pointer import table passed at bind** | **chosen**: it is the D14 consumer-port shape the whole tree already uses; the image's undefined symbols stay inside its own object set, which makes the isolation guard (§7) a one-line `nm -u` check |

```c
/* Lives in edge/module_host.h - the seam between resident core and image. */
typedef struct edge_module_imports {
    uint16_t abi_version;              /* major.minor packed, checked by the host */
    void *host;                        /* opaque, returned to the image's entry */
    void (*push_event)(void *host, const edge_event_t *event);
    uint64_t (*now)(void *host);
    void (*log)(void *host, uint32_t level, const char *text);
    /* append-only (D40): a new import is a new field at the end */
} edge_module_imports_t;
```

The image entry takes it as its only argument and returns
`edge_module_t *` plus its descriptor; it never touches `sys`, `board` or `infra`.

### B. One instance per slot image; the host owns the data area

- Preamble carries `text_size`, `rodata_size`, `data_size`, `bss_size`, and the
  image's own `p_bss_start`/`p_bss_end`.
- Bind: validate magic/ABI/CRC → **zero the image's `.bss`** → copy nothing (an
  in-place image is already at its linked address) → hand the image a private-RAM
  slice from a caller-owned arena → call the entry with the import table → register
  the returned `edge_module_t`s with `sys` through the existing explicit path.
- Multi-instance is **not** supported at Level 2 (the .data/.bss would have to be
  relocated per instance, which is `memory_load`); a product that needs two
  instances ships two slots. Stated in the ADR, not discovered at runtime.
- No heap anywhere (D21): the arena is a product-declared array; `size` comes from
  the preamble and is checked against the remaining arena on bind.

### C. Module ids stay in the app block; the slot has its own identity

`edge/modules.h` reserves `0x9000-0xFFFF` as non-allocatable and `check_module_ids.py`
rejects out-of-block values. A loadable image *is* an app (D73), so its modules keep
app-block ids and nothing in the id table changes. The preamble's `module_id` and
`slot_index` are the image's identity; `slot_index` indexes the linker-script slot
table and nothing else. (This answers #241 open question 4 with "no new block".)

### D. CRC covers what the linker emitted, not what a human typed

`image_crc` is computed by a build-time tool over `.text` + `.rodata` of the slot
ELF and written into a *generated* preamble source file, so the value in flash
cannot drift from the bytes. A CRC failure at bind is a rejected bind
(`EDGE_EINVAL`/`EDGE_EBADIMAGE`), never a partial start; the demo asserts it with a
negative fixture that flips one byte.

## 4. Level by level: what lands, where, how big

### Level 1 - logical module (Phase A, ~3-4 h)

| File | Change |
|---|---|
| `edge_module/include/edge/desc.h` | append `uint16_t abi_version; uint16_t reserved; size_t private_size;` (D40 append-only), add `edge_module_desc_validate()` |
| `edge_module/include/edge/module.h` | `_Static_assert` on the new size |
| `app/dlt645/src/dlt645.c`, `app/relay/src/relay.c` | fill the new fields in `*_descriptor()` |
| `tests/test_module_desc.c` | cases: missing abi, zero private_size, oversized private_size |
| `docs/module-levels.md` | this file, promoted from proposal to decision once D89 is frozen |
| conformance row | `D89` → 🟡 with the descriptor + guard as evidence |

No loader, no linker change, no behaviour change for existing products.

### Level 2 - in-place slot (Phase B, ~2-3 days)

| Piece | Where | Notes |
|---|---|---|
| `edge_module_image_hdr_t` + magic/version/flags | `edge_module/include/edge/module_image.h` | mirrors the ThreadX preamble, 32-bit words, append-only |
| slot linker script | `.github/arm/cortex-m4-slot.ld` | slot region carved out of the existing 64 KiB map; `ASSERT` that slot + core ≤ FLASH |
| image build | `edge_add_module_image(name product slot_index)` in `CMakeLists.txt` | builds a second ELF linked at the slot ORIGIN, generates the preamble from the ELF, objcopies to `.bin`, embeds it into the firmware's slot section |
| demo image | `product/meter_slot_relay/` | composes the **existing** `app/relay` into an image - no new app logic; a slot image is a product, so no new layer |
| demo firmware | `meter_mps2_slot` product + `edge_add_arm_firmware` | binds slot 0, starts it, polls it, stops/unloads it, then the existing exit-status smoke asserts 0 |
| host | `sys/module_host/` (its own area target, D88) | answers #241 open question 2: shared by families, not per-family; `init(arena)`, `bind_inplace`, `start`, `stop`, `unload` |
| guards | `.github/scripts/check_module_image_isolation.py` | `nm -u <slot>.elf` undefined set ⊆ {`memcpy`,`memset`,`memmove`,`__aeabi_*`}; plus preamble self-test and CRC-reject fixture under `tests/guards/module_image_*` |
| budget | `ci/size-budget.json` + `check_map_budget.py` | new `slot` layer budget so the slot cannot silently grow into the core's headroom |

Measured starting point for the budget: core text 6728 B, so `slot = 8192 B flash,
2048 B ram` leaves the core + one slot ≈ 15 K flash of 64 K.

**What the demo proves, honestly:** validation, bind, registration, participation in
poll/events, stop/unload, CRC rejection. It does **not** prove delivery - the image
is flashed with the firmware. Delivery is #26 (bootloader/OTA), and the slot map is
the contract the two share.

### Level 3 - arena + MPU + service gate (Phase C 1-2 days, Phase D 2+ weeks)

- **Phase C (C1)**: whitelist service gate (`EDGE_SVC_*`), module-side stubs,
  `check_module_image_symbols.py` proving the image imports nothing but stubs.
  Runnable *without* an MPU - it is a software contract, which is why it ships first.
- **Phase D**: fixed arena with bump/slab carve per instance, MPU regions programmed
  by `pal`/`board`, fault → `FAULTED` + notify, and the product decision of whether a
  module may run unprivileged. Needs `soc/mps2` MPU support (today: none) *and* a
  second board to prove portability, otherwise it becomes a one-board special.

## 5. Guards by phase

| Phase | Guard | Fixture |
|---|---|---|
| A | `check_module_desc.py` (abi_version present, private_size non-zero, size ≤ arena constant) | `module_desc_good/bad` |
| B | `check_module_image_isolation.py` (undefined symbols) | `module_image_good/bad_symbol` |
| B | preamble/CRC self-test in `tests/` | `module_image_bad_crc` (flipped byte) |
| B | `check_map_budget.py` slot layer | `map_budget_*` extended |
| C | `check_module_image_symbols.py` (service-id whitelist, no resident symbols) | `module_symbols_good/bad` |

Every one of these plugs into `run_guard_selftest.py` and `check_guard_coverage.py`,
which already require a positive *and* a negative fixture.

## 6. Recommendation

Ship **Level 1 alone** if the goal is documentation and metadata (Phase A equals the
"Level 1 shippable without linker/MPU changes" acceptance box). Ship **1 + 2** if the
product outcome is "upgrade a business app without rebuilding the certified core",
because that outcome only exists at Level 2. **Do not** start Level 3 before there is
a board with an MPU in the tree and a product that actually loads third-party code -
otherwise it is a one-board special built on zero existing MPU code.

Estimated total for 1 + 2: **3 working days** (1.5 days if the demo reuses `relay`
exactly as proposed and the QEMU smoke cooperates).

## 7. Open questions left for maintainers (3, not 7)

1. `D89` wording: is Level 1 "a descriptor extension" or "a module model"? It changes
   whether the conformance row is 🟡 for D89 alone or for D89-D91 together.
2. Does the slot image live in the D45 core file list? (`threat-model.md` §7 asks the
   same question; #241 cannot answer it alone.)
3. Is an 8 KiB slot on the MPS2 demo acceptable as the reference number, or should
   the first slot be sized to the largest shipped app (`app/dlt645` text size) so the
   demo cannot pass with a slot only the demo needs?

## 8. Verification status of this document

- `meter_mps2_fw` size and the 64 K/16 K map: **measured locally** (Release, ARM
  toolchain, `arm-none-eabi-size`).
- ThreadX preamble/load/MPU semantics: **checked against upstream docs and headers**
  (links in §2), not from memory.
- `qemu-system-arm -M mps2-an386 -semihosting-config enable=on,target=native` did
  **not** exit within 30 s in this sandbox, while the CI job asserts exit 0. Not
  resolved here; Phase B's demo verification therefore relies on the
  `embedded-smoke` job until that is investigated.

## 9. Related

- #241 (this design), #242 (runner parity - a loadable module makes the observation
  schema more valuable, not less), #243 (`threat-model.md` deny list and checklist),
  #93 MPU, #95 SVC, #26 OTA delivery, #136/#29 certification boundary, D5, D14, D15,
  D21, D40, D45, D47, D50, D73, D76
