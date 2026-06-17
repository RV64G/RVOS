#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stddef.h>
#include <stdint.h>

static inline void *memset(void *ptr, int value, size_t size)
{
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i++)
    {
        bytes[i] = (uint8_t)value;
    }
    return ptr;
}

static inline void *memcpy(void *dst, const void *src, size_t size)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    for (size_t i = 0; i < size; i++)
    {
        out[i] = in[i];
    }
    return dst;
}

static inline void memzero(void *ptr, size_t size)
{
    memset(ptr, 0, size);
}

#endif
