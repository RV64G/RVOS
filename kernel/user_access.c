#include "user_access.h"

#include "csr.h"

#define SSTATUS_SIE (1ULL << 1)
#define SSTATUS_SUM (1ULL << 18)

static uint64_t user_access_begin(void)
{
    uint64_t old_sstatus = csr_read_sstatus();

    /*
     * trap 入口会自动清 SIE，但 copy_from_user() 以后也可能被普通内核路径复用。
     * 先关 SIE 再打开 SUM，避免中断 handler 在 SUM=1 的窗口里运行。
     */
    csr_clear_sstatus(SSTATUS_SIE);
    csr_set_sstatus(SSTATUS_SUM);
    return old_sstatus;
}

static void user_access_end(uint64_t old_sstatus)
{
    if ((old_sstatus & SSTATUS_SUM) == 0)
    {
        csr_clear_sstatus(SSTATUS_SUM);
    }

    if ((old_sstatus & SSTATUS_SIE) != 0)
    {
        csr_set_sstatus(SSTATUS_SIE);
    }
}

int copy_from_user(void *kernel_dst, const void *user_src, uint64_t size)
{
    if (size == 0)
    {
        return 1;
    }

    if (!kernel_dst || !user_src)
    {
        return 0;
    }

    char *dst = kernel_dst;
    const char *src = user_src;
    uint64_t old_sstatus = user_access_begin();

    for (uint64_t i = 0; i < size; i++)
    {
        dst[i] = src[i];
    }

    user_access_end(old_sstatus);
    return 1;
}

int copy_to_user(void *user_dst, const void *kernel_src, uint64_t size)
{
    if (size == 0)
    {
        return 1;
    }

    if (!user_dst || !kernel_src)
    {
        return 0;
    }

    char *dst = user_dst;
    const char *src = kernel_src;
    uint64_t old_sstatus = user_access_begin();

    for (uint64_t i = 0; i < size; i++)
    {
        dst[i] = src[i];
    }

    user_access_end(old_sstatus);
    return 1;
}
