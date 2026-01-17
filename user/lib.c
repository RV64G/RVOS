#include "lib.h"
#include <stdint.h>

/* 
 * User Space Standard Library
 * These functions are compiled into the user section (.user_text)
 * so they can be safely called by user tasks.
 */

size_t strlen(const char *s)
{
	size_t len = 0;
	while (s[len] != '\0')
	{
		len++;
	}
	return len;
}

void *memset(void *dst, int c, size_t n)
{
	char *cdst = (char *)dst;
	for (size_t i = 0; i < n; i++)
	{
		cdst[i] = c;
	}
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	char *cdst = (char *)dst;
	const char *csrc = (const char *)src;
	for (size_t i = 0; i < n; i++)
	{
		cdst[i] = csrc[i];
	}
	return dst;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2))
	{
		s1++;
		s2++;
	}
	return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
	char *ret = dest;
	while ((*dest++ = *src++));
	return ret;
}
