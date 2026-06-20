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

static inline void *memmove(void *dst, const void *src, size_t size)
{
    /*
     * memmove 允许源区间和目标区间重叠。把 void * 转成字节指针后，才能按
     * out[i]/in[i] 逐字节访问。
     */
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    if (out == in || size == 0)
    {
        return dst;
    }

    /*
     * dst 在 src 前面时，从前往后拷贝不会破坏还没读取的源数据；dst 在 src 后面时，
     * 必须从后往前拷贝，否则前面的写入可能覆盖后面仍要读取的源字节。
     */
    if (out < in)
    {
        for (size_t i = 0; i < size; i++)
        {
            out[i] = in[i];
        }
        return dst;
    }

    for (size_t i = size; i > 0; i--)
    {
        out[i - 1] = in[i - 1];
    }
    return dst;
}

static inline int memcmp(const void *left, const void *right, size_t size)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    for (size_t i = 0; i < size; i++)
    {
        if (a[i] != b[i])
        {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

static inline void *memchr(const void *ptr, int value, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)ptr;

    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] == (uint8_t)value)
        {
            return (void *)(uintptr_t)&bytes[i];
        }
    }
    return 0;
}

static inline size_t strlen(const char *s)
{
    size_t len = 0;

    while (s[len] != '\0')
    {
        len++;
    }
    return len;
}

static inline size_t strnlen(const char *s, size_t max_len)
{
    size_t len = 0;

    while (len < max_len && s[len] != '\0')
    {
        len++;
    }
    return len;
}

static inline char *strrchr(const char *s, int ch)
{
    const char *last = 0;

    for (;;)
    {
        if (*s == (char)ch)
        {
            last = s;
        }
        if (*s == '\0')
        {
            break;
        }
        s++;
    }

    return (char *)(uintptr_t)last;
}

static inline void memzero(void *ptr, size_t size)
{
    memset(ptr, 0, size);
}

#endif
