#ifndef __UAPI_PRINTF_H__
#define __UAPI_PRINTF_H__

#include "stdarg.h"

int printf(const char *format, ...);
int vsprintf(char *out, const char *format, va_list args);

#endif // __UAPI_PRINTF_H__
