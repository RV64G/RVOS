#ifndef __STRING_H__
#define __STRING_H__

#include <stddef.h>  // for size_t
#include "kernel/types.h"

/* NULL definition */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* For extra safety, you could define a const null pointer */
#define CONST_NULL ((const void*)0)

/* size_t is now defined in stddef.h */

/* Debug version that might help catch errors */
#ifdef DEBUG_NULL_ACCESS
#define DEBUG_NULL ((volatile void*)0)  /* volatile访问会更明显 */
#endif

/* Standard C library string functions */
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *s);

/* Memory functions (implemented in Rust) */
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* __STRING_H__ */
