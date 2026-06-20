#ifndef COMPILE_CHECK_H
#define COMPILE_CHECK_H

#include <stddef.h>

/*
 * C 结构体和汇编常量共用同一段内存布局时，用这些宏在编译期校验偏移和大小。
 * 一旦 C 字段顺序改了但汇编 offset 没同步，编译会直接失败。
 */
#define CHECK_STRUCT_OFFSET(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), \
                   #type "." #field " offset mismatch")

#define CHECK_STRUCT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), #type " size mismatch")

#endif
