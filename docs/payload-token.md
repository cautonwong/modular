# Payloads without pointers: the token rule (D66)

An event is a **scalar fact** (D16–D18): `{id, source, arg0, arg1, timestamp}`, 24
bytes, no pointers. That is what makes the event queue safe across an ISR boundary
and testable on the host.

But real interfaces carry payloads — a received frame, a measurement block, a
signature. This document is the rule for those (D66), and it exists because it is
the one piece a legacy interface will reach for first: the tempting fix is to put
a pointer in `arg0` and hand the buffer over.

## The rule

```
producer (ISR / driver)                     consumer (app)
   owns a buffer (ring, DMA, static)
   fills it
   pushes {id, arg0 = len, arg1 = generation}  ──►  on_event sees the event
                                                    reads the payload through a
                                                    port it defines itself
```

1. **The data stays in the producer's buffer.** The event says *"a frame arrived,
   this long, this generation"* — it never says *where*.
2. **The consumer reads it through a port the consumer defines** (D14). The
   framework does not know the payload's shape, so it cannot and does not carry
   it. `edge/ports.h` has the canonical shape when the payload is plain bytes:
   `edge_byte_reader_t`.
3. **Lifecycle**: the buffer is valid from the push until the consumer reads it,
   and it is **invalidated by the next event with the same id**. The producer is
   free to reuse the buffer immediately after that.
4. **Therefore the consumer copies in `on_event`.** Anything it needs after the
   callback returns, it copies into its own storage.

## The anti-pattern, and why the gates cannot catch it

```c
/* WRONG - a pointer is just an integer to every check in this repository */
edge_event_t ev = { .id = EDGE_EVT_UART0_RX, .arg0 = (uint32_t)(uintptr_t)buf };
```

`check_event_payload.py` enforces that the *struct* has scalar fields; it cannot
tell a length from an address. And an address pushed from an ISR names a frame
that is gone by the time the runner reads the event, on a different stack. This is
the one rule in the event model that is documented rather than enforced, so it is
documented where an implementer actually looks: here, and in the header next to
`edge_event_t`.

## Make the token a generation, and the rule becomes checkable

The lifecycle rule is a discipline only if the consumer cannot tell a fresh frame
from a stale one. Give the producer a counter and hand the number out as the token,
and the consumer's port can refuse anything old:

```c
/* producer */
event = (edge_event_t){ .id = EDGE_EVT_UART0_RX, .arg0 = rx->length, .arg1 = rx->generation };
/* consumer-defined port: token in, payload out */
typedef struct frame_reader {
    edge_status_t (*read)(void *self, uint32_t token, void *buf, size_t len, size_t *out_len);
    void *self;
} frame_reader_t;
```

The port compares `token` with the producer's current generation and returns
`EDGE_ESTATE` when it no longer matches. Now "read before the next event with the
same id" is a **checked** precondition instead of a comment —
`tests/test_payload_token.c` exercises both halves.

## Mapping a legacy interface onto this

A legacy callback shaped `void on_frame(const uint8_t *buf, size_t len)` is the
case this rule exists for. It becomes two things:

| legacy part | where it goes |
|---|---|
| the buffer it passed | the producer's buffer (`soc/` driver or `infra/` module); the pointer parameter disappears |
| the callback registration | a consumer-defined port (D14), adapted in the composition root |
| "the frame arrived" | an event with `arg0 = len` (and `arg1 = generation` when the producer can supply one) |
| the callback body | the app's `on_event`, which reads through its port and copies what it keeps |

If the legacy code **keeps** the pointer beyond the call — a queue of buffers, a
deferred parse — then the two sides are in different lifetimes and the design has
to change rather than translate. That is the honest boundary of this rule, and it
is where a migration should stop and think instead of adapting mechanically.
