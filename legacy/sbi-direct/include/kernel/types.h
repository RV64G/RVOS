#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>  // 使用标准的类型定义
#include <stddef.h>  // 包含size_t等

// 使用标准类型，避免重复定义
// typedef unsigned char uint8_t;     // 已在stdint.h中定义
// typedef unsigned short uint16_t;   // 已在stdint.h中定义
// typedef unsigned int  uint32_t;    // 已在stdint.h中定义
// typedef unsigned long long uint64_t; // 已在stdint.h中定义
// typedef uint64_t uintptr_t;        // 已在stdint.h中定义

/*
 * RISCV64: register is 64bits width
 */ 
typedef uint64_t reg_t;

/**
 * container_of - 从一个结构体成员的指针获取其宿主结构体的指针
 * @ptr:    指向成员的指针。
 * @type:   宿主结构体的类型。
 * @member: 成员在宿主结构体中的名称。
 *
 * 其实现原理是：用成员的地址(ptr)减去成员在结构体内的偏移量(offsetof)，
 * 从而得到结构体的起始地址。
 */
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type, member) ); \
})

#endif /* __TYPES_H__ */
