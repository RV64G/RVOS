#include "syscall.h"

#include <stdint.h>

#include "console.h"
#include "csr.h"
#include "printk.h"
#include "user_access.h"

#define SYS_READ  63ULL
#define SYS_WRITE 64ULL
#define SYS_EXIT  93ULL

#define SYS_WRITE_CHUNK_SIZE 128ULL

#define SSTATUS_SIE (1ULL << 1)

static long sys_write(uint64_t fd, uint64_t user_buf, uint64_t count)
{
    if (fd != 1 && fd != 2)
    {
        return -1;
    }

    char chunk[SYS_WRITE_CHUNK_SIZE + 1];
    uint64_t copied = 0;

    while (copied < count)
    {
        uint64_t remaining = count - copied;
        uint64_t chunk_size =
            remaining < SYS_WRITE_CHUNK_SIZE ? remaining : SYS_WRITE_CHUNK_SIZE;

        if (!copy_from_user(chunk, (const void *)(uintptr_t)(user_buf + copied),
                            chunk_size))
        {
            return -1;
        }

        /*
         * printk() 接收 C 字符串；write() 接收的是任意长度缓冲区，不要求用户缓冲区
         * 自带 '\0'。所以这里分块复制到内核栈，再补一个临时字符串结尾。
         */
        chunk[chunk_size] = '\0';
        printk(chunk);
        copied += chunk_size;
    }

    return (long)count;
}

static void sys_exit(uint64_t code)
{
    printk("\r\nUser program exited\r\n");
    printk_dec_field("exit_code", code);

    /*
     * exit 是当前用户态 demo 的终点。这里不返回用户态，而是进入临时 console
     * debug loop。后续有 task 回收和 wait/exit 语义后，再改成唤醒父进程。
     */
    csr_write_sscratch(0);
    csr_set_sstatus(SSTATUS_SIE);
    console_debug_loop();
}

void syscall_handle(struct trap_frame *frame)
{
    uint64_t number = frame->a7;
    long ret = -1;

    /*
     * ecall 是一条固定 32-bit 指令。处理完成后 sepc 必须前进，否则 sret 回去会再次
     * 执行同一条 ecall。
     */
    frame->sepc += 4;

    switch (number)
    {
    case SYS_WRITE:
        ret = sys_write(frame->a0, frame->a1, frame->a2);
        break;
    case SYS_EXIT:
        sys_exit(frame->a0);
        break;
    case SYS_READ:
    default:
        printk("Unknown syscall\r\n");
        printk_field("number", number);
        ret = -1;
        break;
    }

    frame->a0 = (uint64_t)ret;
}
