#ifndef KERNEL_ALIGN_H
#define KERNEL_ALIGN_H

#include <stdint.h>

static inline uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static inline uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static inline int is_aligned(uint64_t value, uint64_t align)
{
    return (value & (align - 1)) == 0;
}

#endif
