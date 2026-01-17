#include "kernel.h"
#include "kernel/mm.h"
#include "kernel/types.h"
#include "kernel/printk.h"
#include "arch/riscv.h"
#include "string.h"

/*
 * RISC-V Sv39 Scheme
 * ...
 */

// Use correct linker symbols (arrays, so they resolve to addresses)
extern char _text_start[];
extern char _text_end[];
extern char _user_text_start[];
extern char _user_text_end[];
extern char _memory_end[];

// Global Kernel Page Table
pagetable_t kernel_pagetable;

/*
 * Return the address of the PTE in page table 'pagetable'
 * that corresponds to virtual address 'va'.  If alloc!=0, 
 * create any required page-table pages.
 */
pte_t *walk(pagetable_t pagetable, uint64_t va, int alloc)
{
	if (va >= MAXVA)
		panic("walk");

	for (int level = 2; level > 0; level--)
	{
		pte_t *pte = &pagetable[PX(level, va)];
		if (*pte & PTE_V)
		{
			pagetable = (pagetable_t)PTE2PA(*pte);
		}
		else
		{
			if (!alloc || (pagetable = (pagetable_t)page_alloc(1)) == NULL)
				return NULL;
			
			memset(pagetable, 0, PAGE_SIZE);
			*pte = PA2PTE(pagetable) | PTE_V;
		}
	}
	return &pagetable[PX(0, va)];
}

/*
 * Create a specific Page Table Entry (PTE) for a virtual address
 * which refers to a physical address.
 */
int kvmmap(pagetable_t pagetable, uint64_t va, uint64_t pa, uint64_t sz, int perm)
{
	uint64_t a, last;
	pte_t *pte;

	a = PGROUNDDOWN(va);
	last = PGROUNDDOWN(va + sz - 1);

	for (;;)
	{
		if ((pte = walk(pagetable, a, 1)) == NULL)
			return -1;
		
		if (*pte & PTE_V) {
			// Mapping exists, silently update permissions/PA
		}

		*pte = PA2PTE(pa) | perm | PTE_V;

		if (a == last)
			break;
		a += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
	return 0;
}

/*
 * Initialize the kernel page table.
 */
void kvminit()
{
	// 1. Allocate the root page table (Level 2)
	kernel_pagetable = (pagetable_t)page_alloc(1);
	if (kernel_pagetable == NULL)
        panic("kvminit: out of memory");
        
	memset(kernel_pagetable, 0, PAGE_SIZE);

	printk("Setting up Kernel Page Tables...\n");

	// --- 1. Map UART (0x10000000) ---
	printk("Mapping UART...\n");
	kvmmap(kernel_pagetable, UART0, UART0, PAGE_SIZE, PTE_R | PTE_W | PTE_A | PTE_D);

    // --- 2. Map CLINT (0x2000000) ---
    printk("Mapping CLINT...\n");
    kvmmap(kernel_pagetable, 0x02000000, 0x02000000, 0x10000, PTE_R | PTE_W | PTE_A | PTE_D);

    // --- 3. Map PLIC (0x0c000000) ---
    printk("Mapping PLIC...\n");
    kvmmap(kernel_pagetable, 0x0c000000, 0x0c000000, 0x400000, PTE_R | PTE_W | PTE_A | PTE_D);

	// --- 4. Map ALL RAM (Identity Mapping) ---
	// We map the entire RAM as RWX | PTE_U first. 
    // This allows the kernel and users to access memory.
	printk("Mapping ALL RAM (Identity)...\n");
	kvmmap(kernel_pagetable, (uint64_t)_text_start, (uint64_t)_text_start, 
           (uint64_t)_memory_end - (uint64_t)_text_start, PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);

    // --- 5. Refine Kernel Text (RX, Supervisor) ---
    // Remove W (Write) and U (User) permissions from kernel code
    printk("Refining Kernel Text (RX, Supervisor)...\n");
    kvmmap(kernel_pagetable, (uint64_t)_text_start, (uint64_t)_text_start, 
           (uint64_t)_text_end - (uint64_t)_text_start, PTE_R | PTE_X | PTE_A | PTE_D);
           
    // --- 6. Expose User Text (RX, User) ---
    // Add U (User) permission back ONLY for the .user_text section
    printk("Refining User Text (RX, User)...\n");
    if (_user_text_end > _user_text_start) {
        kvmmap(kernel_pagetable, (uint64_t)_user_text_start, (uint64_t)_user_text_start, 
               (uint64_t)_user_text_end - (uint64_t)_user_text_start, PTE_R | PTE_X | PTE_U | PTE_A | PTE_D);
    }
    
    printk("Kernel Page Tables created.\n");
}

/*
 * Switch h/w page table register to the kernel's page table,
 * and enable paging.
 */
void kvminithart()
{
	w_satp(MAKE_SATP(kernel_pagetable));
	sfence_vma();
    // Flush instruction cache to ensure we fetch from the new mappings
    asm volatile("fence.i");
    printk("MMU Enabled (satp set).\n");
}
