#ifndef __USER_LIB_H__
#define __USER_LIB_H__

#include <stddef.h>

/* 
 * Rename user-space functions to avoid symbol collision with kernel functions 
 * when linking into the same ELF binary.
 */
#define strlen u_strlen
#define memset u_memset
#define memcpy u_memcpy
#define strcmp u_strcmp
#define strcpy u_strcpy

size_t u_strlen(const char *s);
void *u_memset(void *dst, int c, size_t n);
void *u_memcpy(void *dst, const void *src, size_t n);
int u_strcmp(const char *s1, const char *s2);
char *u_strcpy(char *dest, const char *src);

#endif /* __USER_LIB_H__ */