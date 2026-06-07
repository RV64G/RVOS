#include "string.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    if (!dest || !src) {
        return dest;
    }
    
    char *d = (char *)dest;
    const char *s = (const char *)src;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    if (!s) {
        return s;
    }
    
    unsigned char *ptr = (unsigned char *)s;
    unsigned char value = (unsigned char)c;
    
    for (size_t i = 0; i < n; i++) {
        ptr[i] = value;
    }
    
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)(p1[i] - p2[i]);
        }
    }
    
    return 0;
}

size_t strlen(const char *s)
{
    if (!s) {
        return 0;
    }
    
    size_t len = 0;
    const size_t MAX_STRING_LENGTH = 65536; // 64KB 限制
    
    while (len < MAX_STRING_LENGTH && s[len] != '\0') {
        len++;
    }
    
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) {
        return s1 ? 1 : (s2 ? -1 : 0);
    }
    
    size_t i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        i++;
    }
    
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    if (!s1 || !s2) {
        return s1 ? 1 : (s2 ? -1 : 0);
    }
    
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    if (!dest || !src) {
        return dest;
    }
    
    size_t i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    
    return dest;
}

char *strcat(char *dest, const char *src)
{
    if (!dest || !src) {
        return dest;
    }
    
    size_t dest_len = strlen(dest);
    strcpy(dest + dest_len, src);
    
    return dest;
}

char *strrchr(const char *s, int c)
{
    if (!s) {
        return NULL;
    }
    
    size_t len = strlen(s);
    unsigned char ch = (unsigned char)c;
    
    for (size_t i = len; i > 0; i--) {
        if ((unsigned char)s[i-1] == ch) {
            return (char *)(s + i - 1);
        }
    }
    
    if (ch == '\0') {
        return (char *)(s + len);
    }
    
    return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return NULL;
    }
    
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }
    
    size_t haystack_len = strlen(haystack);
    if (needle_len > haystack_len) {
        return NULL;
    }
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (strncmp(haystack + i, needle, needle_len) == 0) {
            return (char *)(haystack + i);
        }
    }
    
    return NULL;
}

void fast_memzero(void *s, size_t n)
{
    memset(s, 0, n);
}
