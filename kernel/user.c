#include "user.h"

#include <stdint.h>

#include "early_vm.h"
#include "page_alloc.h"
#include "printk.h"
#include "vm.h"

#define USER_STACK_TOP  0x40000000ULL
#define USER_STACK_PAGES 2ULL
#define USER_TRAP_STACK_PAGES 2ULL

extern char __user_start[];
extern char __user_end[];
extern void user_demo_start(void);
extern void riscv_enter_user(uint64_t entry, uint64_t stack_top,
                             uint64_t trap_stack_top);

static int map_user_section(void)
{
    uint64_t start = (uint64_t)(uintptr_t)__user_start;
    uint64_t end = (uint64_t)(uintptr_t)__user_end;

    if (end <= start)
    {
        printk("User section is empty\r\n");
        return 0;
    }

    if (!vm_identity_map(kernel_vm_space(), start, end - start,
                         VM_MAP_READ | VM_MAP_EXEC | VM_MAP_USER))
    {
        printk("User section mapping failed\r\n");
        return 0;
    }

    return 1;
}

int user_demo_run(void)
{
    void *user_stack_phys = phys_alloc_pages(USER_STACK_PAGES);
    void *trap_stack_phys = phys_alloc_pages(USER_TRAP_STACK_PAGES);
    uint64_t user_stack_size = USER_STACK_PAGES * VM_PAGE_SIZE;
    uint64_t trap_stack_size = USER_TRAP_STACK_PAGES * VM_PAGE_SIZE;
    uint64_t user_stack_base = USER_STACK_TOP - user_stack_size;

    if (!user_stack_phys || !trap_stack_phys)
    {
        if (user_stack_phys)
        {
            phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        }
        if (trap_stack_phys)
        {
            phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        }
        printk("User demo stack allocation failed\r\n");
        return 0;
    }

    if (!map_user_section())
    {
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    if (!vm_map_range(kernel_vm_space(), user_stack_base,
                      (uint64_t)(uintptr_t)user_stack_phys,
                      user_stack_size,
                      VM_MAP_READ | VM_MAP_WRITE | VM_MAP_USER))
    {
        printk("User stack mapping failed\r\n");
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    vm_flush_all();

    printk("Entering first U-mode program\r\n");
    riscv_enter_user(
        (uint64_t)(uintptr_t)user_demo_start,
        USER_STACK_TOP,
        (uint64_t)(uintptr_t)trap_stack_phys + trap_stack_size
    );

    return 0;
}
