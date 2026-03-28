#include "ring_buffer.h"

/*
 * Memory barrier — on RP2040 this would be __dmb().
 * On Linux sim we use __sync_synchronize() (GCC built-in).
 */
#ifdef SIM_BUILD
    #define MEMORY_BARRIER() __sync_synchronize()
#else
    #define MEMORY_BARRIER() __dmb()
#endif

void ring_init(ring_buffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

bool ring_push(ring_buffer_t *rb, int32_t sample)
{
    uint32_t next = (rb->head + 1) % RING_BUFFER_SIZE;

    if (next == rb->tail) {
        return false; /* Full */
    }

    rb->data[rb->head] = sample;
    MEMORY_BARRIER();
    rb->head = next;
    return true;
}

bool ring_pop(ring_buffer_t *rb, int32_t *sample)
{
    if (rb->head == rb->tail) {
        return false; /* Empty */
    }

    *sample = rb->data[rb->tail];
    MEMORY_BARRIER();
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    return true;
}

uint32_t ring_available(const ring_buffer_t *rb)
{
    uint32_t h = rb->head;
    uint32_t t = rb->tail;
    if (h >= t)
        return h - t;
    else
        return RING_BUFFER_SIZE - t + h;
}

bool ring_is_full(const ring_buffer_t *rb)
{
    return ((rb->head + 1) % RING_BUFFER_SIZE) == rb->tail;
}
