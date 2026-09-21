#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"

/*
 * The token/payload rule (D66): the event carries scalars only, the payload stays
 * in the producer's buffer, and the consumer reads it through a port it defines
 * itself. Two things are pinned here that a comment cannot pin:
 *
 *   1. the round trip works - a token is enough to fetch the payload;
 *   2. the lifecycle rule is *checkable* when the token is a generation - the port
 *      refuses a token whose frame is gone, so "read before the next event with the
 *      same id" is a checked precondition rather than a discipline.
 */

typedef struct rx_producer {
    uint8_t buffer[32];
    uint32_t length;
    uint32_t generation;
} rx_producer_t;

/* The consumer defines this port (D14). The framework never sees the payload. */
typedef struct frame_reader {
    edge_status_t (*read)(void *self, uint32_t token, void *buf, size_t len, size_t *out_len);
    void *self;
} frame_reader_t;

static edge_status_t reader_read(void *self, uint32_t token, void *buf, size_t len,
                                 size_t *out_len) {
    rx_producer_t *rx = (rx_producer_t *)self;
    if (rx == NULL || buf == NULL)
        return EDGE_EINVAL;
    if (token != rx->generation)
        return EDGE_ESTATE; /* the frame that token named is gone */
    if (len < rx->length)
        return EDGE_EOVERFLOW;
    for (uint32_t i = 0u; i < rx->length; ++i)
        ((uint8_t *)buf)[i] = rx->buffer[i];
    if (out_len != NULL)
        *out_len = rx->length;
    return EDGE_OK;
}

/* Producing a frame bumps the generation, which invalidates every token handed out
 * for the previous one. */
static void produce(rx_producer_t *rx, const char *text, uint32_t len) {
    for (uint32_t i = 0u; i < len; ++i)
        rx->buffer[i] = (uint8_t)text[i];
    rx->length = len;
    ++rx->generation;
}

static void test_token_round_trip(void **state) {
    (void)state;
    rx_producer_t rx = {0};
    const frame_reader_t reader = {.read = reader_read, .self = &rx};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t event;
    uint8_t copy[32];
    size_t copied = 0u;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    produce(&rx, "frame-A", 7u);

    /* The producer hands over a token, never an address. */
    const edge_event_t arrived = {
        .id = EDGE_EVT_UART0_RX, .source = 0u, .arg0 = rx.length, .arg1 = rx.generation};
    assert_int_equal(edge_event_push_isr(&queue, &arrived), EDGE_OK);

    assert_int_equal(edge_event_pop(&queue, &event), EDGE_OK);
    assert_int_equal(event.id, EDGE_EVT_UART0_RX);
    assert_int_equal(reader.read(reader.self, event.arg1, copy, sizeof(copy), &copied), EDGE_OK);
    assert_int_equal(copied, 7u);
    assert_memory_equal(copy, "frame-A", 7u);
}

static void test_stale_token_is_refused(void **state) {
    (void)state;
    rx_producer_t rx = {0};
    const frame_reader_t reader = {.read = reader_read, .self = &rx};
    uint8_t copy[32];

    produce(&rx, "frame-A", 7u);
    const uint32_t stale_token = rx.generation;
    produce(&rx, "frame-B", 7u); /* the previous frame is gone */

    assert_int_equal(reader.read(reader.self, stale_token, copy, sizeof(copy), NULL), EDGE_ESTATE);
    assert_int_equal(reader.read(reader.self, rx.generation, copy, sizeof(copy), NULL), EDGE_OK);
    assert_memory_equal(copy, "frame-B", 7u);

    /* A consumer that asks for less than the frame is refused, not truncated:
     * silently returning a prefix is how a frame parser loses a byte. */
    assert_int_equal(reader.read(reader.self, rx.generation, copy, 3u, NULL), EDGE_EOVERFLOW);
}

/*
 * Why the consumer copies: the producer is free to reuse its buffer as soon as the
 * next frame starts, regardless of what the app still holds.
 */
static void test_the_consumers_copy_is_independent(void **state) {
    (void)state;
    rx_producer_t rx = {0};
    const frame_reader_t reader = {.read = reader_read, .self = &rx};
    uint8_t copy[32];
    size_t copied = 0u;

    produce(&rx, "frame-A", 7u);
    assert_int_equal(reader.read(reader.self, rx.generation, copy, sizeof(copy), &copied), EDGE_OK);

    produce(&rx, "frame-Z", 7u); /* the producer reuses its buffer immediately */

    assert_memory_equal(copy, "frame-A", copied); /* the copy still holds the old frame */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_token_round_trip),
        cmocka_unit_test(test_stale_token_is_refused),
        cmocka_unit_test(test_the_consumers_copy_is_independent),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
