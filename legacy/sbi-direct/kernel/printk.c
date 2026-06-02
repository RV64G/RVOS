#include "kernel.h"
#include "riscv/sbi.h"
#include "kernel/printk.h"
#include "string.h"

#define PRINTK_BUF_SIZE 1024

/*
 * ref: https://github.com/cccriscv/mini-riscv-os/blob/master/05-Preemptive/lib.c
 */

static int vsnprintk(char * out, size_t n, const char* s, va_list vl)
{
	int format = 0;
	int longarg = 0;
	size_t pos = 0;
	for (; *s; s++) {
		if (format) {
			switch(*s) {
			case 'l': {
				longarg = 1;
				break;
			}
			case 'p': {
				longarg = 1;
				if (out && pos < n) {
					out[pos] = '0';
				}
				pos++;
				if (out && pos < n) {
					out[pos] = 'x';
				}
				pos++;
				__attribute__((fallthrough));
			}
			case 'x': {
				long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
				int hexdigits = 2*(longarg ? sizeof(long) : sizeof(int))-1;
				for(int i = hexdigits; i >= 0; i--) {
					int d = (num >> (4*i)) & 0xF;
					if (out && pos < n) {
						out[pos] = (d < 10 ? '0'+d : 'a'+d-10);
					}
					pos++;
				}
				longarg = 0;
				format = 0;
				break;
			}
			case 'u':
			case 'd': {
				unsigned long num_to_print;

				if (*s == 'd') {
					long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
					if (num < 0) {
						num_to_print = -num;
						if (out && pos < n) {
							out[pos] = '-';
						}
						pos++;
					} else {
						num_to_print = num;
					}
				} else {
					num_to_print = longarg ? va_arg(vl, unsigned long) : va_arg(vl, unsigned int);
				}

				long digits = 1;
				for (unsigned long nn = num_to_print; nn /= 10; digits++);
				
				for (int i = digits - 1; i >= 0; i--) {
					if (out && pos + i < n) {
						out[pos + i] = '0' + (num_to_print % 10);
					}
					num_to_print /= 10;
				}
				pos += digits;
				longarg = 0;
				format = 0;
				break;
			}
			case 's': {
				const char* s2 = va_arg(vl, const char*);
				while (*s2) {
					if (out && pos < n) {
						out[pos] = *s2;
					}
					pos++;
					s2++;
				}
				longarg = 0;
				format = 0;
				break;
			}
			case 'c': {
				if (out && pos < n) {
					out[pos] = (char)va_arg(vl,int);
				}
				pos++;
				longarg = 0;
				format = 0;
				break;
			}
			default:
				break;
			}
		} else if (*s == '%') {
			format = 1;
		} else {
			if (out && pos < n) {
				out[pos] = *s;
			}
			pos++;
		}
    	}
	if (out && pos < n) {
		out[pos] = 0;
	} else if (out && n) {
		out[n-1] = 0;
	}
	return pos;
}

/**
 * @brief 内核内部的写操作核心实现。
 * @details
 *   此函数负责将指定缓冲区的数据直接写入到控制台。
 *   它直接通过 SBI 调用 `sbi_console_putchar` 来输出字符。
 *   这个函数是所有内核打印和控制台输出的最终端点。
 * @param buf 待写入数据的缓冲区。
 * @param len 待写入数据的长度。
 * @return 成功写入的字节数。
 */
/**
 * @brief 使用可变参数列表的核心打印函数
 * @details
 *   此函数接收一个格式化字符串和一个 va_list，
 *   将格式化后的结果放入一个静态缓冲区，然后通过 do_write 输出。
 * @param fmt 格式化字符串
 * @param args va_list 参数列表
 * @return 打印的字符数
 */
int vprintk(const char *fmt, va_list args)
{
    char buf[PRINTK_BUF_SIZE];
    int len = vsnprintk(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        // 直接输出到控制台
        for (int i = 0; i < len; i++) {
            sbi_console_putchar(buf[i]);
        }
    }
    return len;
}

/**
 * @brief 内核格式化打印函数
 * @details
 *   内核代码应该使用此函数来打印调试信息。
 *   它线程安全（使用栈缓冲区），并将所有输出引导到控制台。
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 打印的字符数
 */
int printk(const char* fmt, ...)
{
    va_list args;
    int len;

    va_start(args, fmt);
    len = vprintk(fmt, args);
    va_end(args);

    return len;
}

void panic(const char *s)
{
	printk("panic: %s\n", s);
	while(1){};
}
