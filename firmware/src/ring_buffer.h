#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
 * Core 0 (ADC) writes, Core 1 (Comms) reads.
 * No mutex needed — uses atomic head/tail with memory barriers.
 */

#define RING_BUFFER_SIZE 256

typedef struct {
    volatile uint32_t head;     /* Written by producer (Core 0) */
    volatile uint32_t tail;     /* Written by consumer (Core 1) */
    int32_t data[RING_BUFFER_SIZE];
} ring_buffer_t;

void    ring_init(ring_buffer_t *rb);
bool    ring_push(ring_buffer_t *rb, int32_t sample);
bool    ring_pop(ring_buffer_t *rb, int32_t *sample);
uint32_t ring_available(const ring_buffer_t *rb);
bool    ring_is_full(const ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
