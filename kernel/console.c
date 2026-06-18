#include "console.h"

#include <stdint.h>

#include "printk.h"

#define CONSOLE_INPUT_CAPACITY 256U

static char input_buffer[CONSOLE_INPUT_CAPACITY];
static volatile uint32_t input_head;
static volatile uint32_t input_tail;

static uint32_t next_index(uint32_t index)
{
    return (index + 1U) % CONSOLE_INPUT_CAPACITY;
}

static int console_getc(int *ch)
{
    if (input_tail == input_head)
    {
        return 0;
    }

    *ch = input_buffer[input_tail];
    input_tail = next_index(input_tail);
    return 1;
}

void console_init(void)
{
    input_head = 0;
    input_tail = 0;
}

void console_input_putc(int ch)
{
    uint32_t next = next_index(input_head);

    /*
     * 缓冲区满时丢弃新字符。中断路径不能等待消费者，也不能在这里打印错误。
     * 等后续 read()/tty 成形后，可以把溢出计数暴露给调试接口。
     */
    if (next == input_tail)
    {
        return;
    }

    input_buffer[input_head] = (char)ch;
    input_head = next;
}

void console_drain_input(void)
{
    int ch;

    while (console_getc(&ch))
    {
        if (ch == '\r' || ch == '\n')
        {
            printk("\r\n> ");
            continue;
        }

        if (ch == '\b' || ch == 0x7f)
        {
            printk("\b \b");
            continue;
        }

        char out[2] = { (char)ch, '\0' };
        printk(out);
    }
}

void console_debug_loop(void)
{
    printk("Console input ready\r\n");
    printk("> ");

    for (;;)
    {
        console_drain_input();
        __asm__ volatile("wfi" ::: "memory");
    }
}
