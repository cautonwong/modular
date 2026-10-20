# Trust boundaries and threat model

Who is trusted, what may cross a boundary, and what is forbidden.

This is a **requirements and review document**, not a certification claim: it
exists so MPU (#93), SVC/privilege (#95), vendor SDK use (#149), OTA (#26),
the certification boundary (#136, #29) and the optional Module mechanism (#241)
all design isolation against the same map instead of against their own
assumptions. Nothing here overrides [`adr.md`](adr.md); where a decision already
exists it is cited, and where none exists yet the gap is named with its ticket.

## 1. What is true today (scope of the current tree)

| Fact | Consequence for this document |
|---|---|
| Static composition only: `main()` builds the product by hand, no loader (D5, D7) | there is no runtime module boundary yet; every "domain" below but `resident core` and `host tools/CI` is *forward-looking* |
| Events carry fixed scalars, no pointers (D16-D18), enforced by `check_event_payload.py` | the only cross-module channel is a bounded, forgeable-but-not-writable fact |
| Zero runtime allocation (D21), enforced by `check_no_dynamic_memory.py` | "queue flood" degrades into drops, not into an allocator failure |
| OTA / bootloader is out of scope by decision (D76) | image authenticity is a **requirement on a future workstream**, not a control that exists |
| No cryptography and no stored secrets in the tree | "keys" is an asset list entry for future products, not a current store |
| Vendors enter only through `soc/` and `pal/`, enforced by `check_sdk_isolation.py` (D44/D49) | a compromised SDK is contained to hardware-bound layers, not to app logic |

## 2. Assets

1. **Safety-relevant state** - actuator outputs (relay, BLDC drive), watchdog feed,
   the module lifecycle state machine (`sys/runtime`), and the certification-core
   code itself.
2. **Secrets** - none in-tree today. Expected later: update-image signing keys
   (verification key only, never a private key, #26) and any per-device identity
   a product adds.
3. **Event integrity** - that a fact was produced by the component claiming it, was
   not reordered across a defined boundary, and was not forged to drive an actuator.
4. **Uptime / boundedness** - that no component can starve the runner, exhaust a
   queue, or turn a fault into a hang (D47, D52, D69, D75).
5. **Audit evidence** - that the certified core matches what was reviewed (D45,
   `ci/exemptions.json`, the SBOM generator).

## 3. Trust domains

| Domain | Trust level | May | Must not |
|---|---|---|---|
| **Resident core** (`edge_module`, `sys`, `board`, `infra`) | trusted, privileged, in D45 audit scope | own all storage, schedule, dispatch, guard ISR boundaries | contain app-specific policy; allocate at runtime (D21) |
| **Board / ISR** | trusted, privileged, RTOS-free (D85) | read MMIO, push events through the sink guard | call the RTOS directly (`check_rtos_isr_safety.py`), block, allocate |
| **RTOS kernel + its port** (`pal/`) | trusted third party | schedule tasks, provide tick/notify | leak into framework code (D50, `check_app_isolation.py`) |
| **Vendor SDK / precompiled `.a`** | **untrusted-by-default** | exist inside `soc/<soc>` or `pal/<name>` | be included or linked by `app/`, `infra/`, `sys/`, `edge_module/` (#149); be trusted without an exemption-registry entry |
| **Product glue** (`product/<name>`) | trusted, per-product | bind concrete adapters, own product configuration (D87) | be reused as a policy layer for other products |
| **App / module** (`app/<name>`) | trusted today, **potentially untrusted** at Module Level 2-3 (#241) | consume its ports, receive events, keep private caller-owned state | see the deny list below - in particular survive a FAILED transition without being polled or dispatched |
| **Host tools / CI** | trusted for evidence, not for firmware | generate SBOM/provenance, run guards, sign releases | be assumed present or honest when reasoning about device behaviour |

Reverse view worth keeping: **the device trusts its own core, and the core
trusts the app's ports - but only the composition root decides which app exists.**
That last sentence is what D5 buys, and it is why the deny list can be short: an
attacker needs the resident image replaced, not just a bad app loaded.

## 4. Channels and the deny list

**Allowed channels**

| Channel | Shape | Bounded by |
|---|---|---|
| Event sink -> queue -> dispatch | scalar `{id, source, arg0, arg1, timestamp}` | queue capacity, per-run budget (D69), dropped counter |
| Consumer-defined ports | function pointers in a per-app struct | caller-owned storage only; `check_consumer_ports.py` |
| Injected clock | `edge_clock_port_t` | no global time read inside the framework (D20) |
| Shared interrupts | `board_irq_attach()`, registration order | fixed static table, bounded walk (D75, D84) |
| Service gate (future, #95) | whitelisted `edge_svc_id_t` calls | deny-by-default table, CI symbol check (#241 D91) |
| Shared memory regions (future, #93) | declare-only descriptors at bind time | MPU regions, programmed by `pal`/`board` (#241 D92) |

**Deny list** (each entry is a rule a future design must not quietly break)

1. Module or app code reading MMIO, or including a vendor header
   (`check_sdk_isolation.py`, `check_layer_dependencies.py`).
2. App calling `sys`, `board` or `infra` symbols directly - the consumer port is
   the only path (D14, D78/D79).
3. Function pointers or buffers inside an event payload (D18).
4. ISR code inside a module image, or module threads as the default (D47, #241).
5. `malloc`/`free`, or any target that pulls in an allocator (D21).
6. Linker-section module discovery (D5, #241 non-goals).
7. Module-to-module calls outside the service gate (#241 D91).
8. A vendor precompiled archive linked into `app/` or `infra/` without a registry
   entry (#149, `ci/exemptions.json`).
9. A FAILED module continuing to be polled or dispatched (D53,
   `sys/runtime/src/sys.c`).

## 5. Top threats (STRIDE-style, brief)

| # | Threat (class) | Scenario | Control today | Gap / ticket |
|---|---|---|---|---|
| T1 | **Tampering** | modified application or firmware image accepted as genuine | none - no loader, no signature check | #26 (requires D40 version + D34 distribution matrix), #241 D90 preamble/CRC for module images only |
| T2 | **Elevation / spoofing** | untrusted module reads MMIO or calls privileged core symbols | not applicable at Level 0-1 (no separate image); layering guards block the source-level form | #93 MPU, #95 SVC gate, #241 Level 2-3 design |
| T3 | **Tampering (supply chain)** | vendor SDK or prebuilt `.a` does something other than its header promises | header/archive sandbox (#149), exemption registry with owner + exit condition, SBOM/provenance generation in CI | no symbol-surface check on a precompiled archive; #149 follow-up |
| T4 | **Denial of service** | event storm from one source starves the runner or drops another module's facts | bounded queue, per-run budget, dropped counters, FAILED-module isolation (D53, D69, D75) | no per-producer fairness; #28 observation work |
| T5 | **Spoofing / repudiation** | forged event drives an actuator; later nobody can tell who produced it | `source` field is carried, events are facts not commands (D16) | no authentication of `source`; acceptable while all producers are resident and trusted, becomes a real threat with #241 Level 3 |
| T6 | **Time abuse** | clock jumped, wrapped, or a stale deadline fires twice | injected clock (D20), wrap-safe comparison (D72), exit codes `14`/`15` for clock anomalies (#126) | no monotonic guarantee from an external RTC; a product that adds one owns the check |
| T7 | **Elevation (privilege)** | ISR path takes a lock, sleeps, or allocates and deadlocks the runner | `check_rtos_isr_safety.py`, sink guard + conditional yield (#85, `docs/rtos-runner.md` §2) | none known |
| T8 | **Information disclosure** | secrets reach a log port or an event payload | log port is injected and app-facing only; no secrets in-tree | no rule yet on what a product may log once it has keys |

## 6. Checklist

Use the column that owns the change; an unchecked box in the owning column blocks
merge.

**Design review (human)**

- [ ] New cross-domain channel: named here, with its bound (size, rate, budget).
- [ ] New deny-list exception: registered in `ci/exemptions.json` with owner,
      reason, exit condition and ticket - the same contract the quality gate uses.
- [ ] Vendor addition: licence, SBOM entry, symbol surface, and whether it lands
      in `soc/` or `pal/` (never `app/`, `infra/`, `sys/`, `edge_module/`).
- [ ] OTA / image work: stated in terms of D76 + D40 - who signs, who verifies,
      what happens on a rejected image, and how rollback is bounded (coordinated
      with #26; this document defines the requirement, not the bootloader).
- [ ] Module work (#241): preamble/CRC/ABI version rules, service whitelist,
      shared regions declared rather than discovered, and an explicit answer to
      "is a Level 2 image inside the D45 audit scope?".
- [ ] ISR additions: stays resident and privileged, RTOS-free, bounded (D75).

**CI (automatic)**

- [ ] `check_layer_dependencies.py`, `check_sdk_isolation.py`,
      `check_event_payload.py`, `check_no_dynamic_memory.py`,
      `check_rtos_isr_safety.py`, `check_consumer_ports.py`, `check_app_purity.py`
      all green.
- [ ] `check_exemptions.py` - every registered exception still points at a real
      target with all fields.
- [ ] `generate_sbom.py` + `generate_build_metadata.py` produce provenance for
      the release image.
- [ ] New guard? Then it ships a negative fixture (`check_guard_coverage.py`).

**Board bring-up (product owner)**

- [ ] IRQ priorities and the RTOS priority ceiling documented for the board.
- [ ] MPU regions (if any) enumerated, and the failure mode on a violation is
      observed, not assumed (#93).
- [ ] Low-power entry/exit does not weaken a boundary above (watchdog and guard
      order, `docs/low-power.md`).
- [ ] Exit-status encoding still distinguishes the fault classes in
      `docs/rtos-runner.md` §6.

## 7. D45 relationship and open questions

D45 scopes **files** (`edge_module` + `sys` + `board` + `infra` are the certifiable
core; `app` is outside), which is an audit boundary, not a runtime trust boundary.
The two must not be confused: a Level 2 module image could sit inside the core's
file list while executing untrusted code.

Open questions for maintainers:

1. Is a Level 2 module image (#241) part of the D45 core for certification
   purposes, or a replaceable artifact with its own declaration?
2. Do Level 3 modules require authenticated `source` on their events (T5), or is
   the service gate enough because a module can only publish facts?
3. Should the exemption registry grow a `trust` field so a vendor archive states
   "untrusted, contained by `soc/`" instead of relying on prose?
4. Where does the OTA verification key live (#26), and does its presence push the
   bootloader into the core file list?
5. Does the host-tools domain need its own guard set (pinned actions exist;
   `check_dependencies.py` covers pins) or is CI trust out of scope by decision?

## 8. Related

- Decisions: [D5](adr.md), [D14](adr.md), [D15](adr.md), [D16-D18](adr.md),
  [D21](adr.md), [D40](adr.md), [D45](adr.md), [D53](adr.md), [D69](adr.md),
  [D71](adr.md), [D75](adr.md), [D76](adr.md), [D84](adr.md)
- Gates: [governance.md](governance.md), `ci/exemptions.json`, `ci/adr-gates.json`
- Ops: [SECURITY.md](../SECURITY.md) (reporting), [rtos-runner.md](rtos-runner.md)
  (runner-side observability), [low-power.md](low-power.md)
- Tickets: #241 optional modules, #93 MPU, #95 SVC/privilege, #26 bootloader/OTA,
  #149 prebuilt libs, #136 / #29 certification boundary, #85 ISR safety
