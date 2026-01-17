#include "arch/platform.h"

	# size of each hart's stack is 1024 bytes
	.equ	STACK_SIZE, 4096

	.global	_start
	.text
_start:
	# In S-mode, OpenSBI passes hart ID via a0 register, not mhartid CSR
	# a0 = hart ID (passed by OpenSBI)
	# a1 = DTB (Device Tree Blob) address
	
	# Save hart ID from a0 register (not from mhartid CSR)
	mv	tp, a0			# keep CPU's hartid in its tp for later usage.
    
    # Force disable paging (MMU) to ensure we are in a clean state
    # This fixes issues when reloading via JTAG where satp might be dirty
    csrw satp, zero
    sfence.vma

	# Check hart ID: if hart 1, normal boot; others start hart 1 and stop themselves
	li	t0, 1
	beq	a0, t0, hart1_normal_boot	# Hart 1 goes to normal boot
	
	# Other harts (0, 2, 3, ...): Start hart 1 and then stop themselves
	j	other_harts_start_hart1_and_stop
	
hart1_normal_boot:
	# Hart 1: Normal boot logic
	# Save hart ID and DTB address to our hart info system
	la	t0, boot_hartid_asm
	sd	a0, (t0)
	
	# Store DTB address to global variable  
	la	t0, dtb_addr_asm
	sd	a1, (t0)

	# Set all bytes in the BSS section to zero.
	la	a0, _bss_start
	la	a1, _bss_end
	bgeu	a0, a1, 2f
1:
	sd	zero, (a0)		# Use 64-bit store (sd) instead of 32-bit (sw)
	addi	a0, a0, 8		# Increment by 8 bytes for 64-bit
	bltu	a0, a1, 1b
2:	# Setup stacks, the stack grows from bottom to top, so we put the
	# stack pointer to the very end of the stack range.
	slli	t0, tp, 10		# shift left the hart id by 1024
	la	sp, stacks + STACK_SIZE	# set the initial stack pointer
					# to the end of the first stack space
	add	sp, sp, t0		# move the current hart stack pointer
					# to its place in the stack space
	# Set sstatus.SPP to 1 (S-mode), so we stay in S-mode after SRET
	# Set sstatus.SPIE to 1, so SRET will enable the interrupt
	# Set sstatus.SUM to 1, so S-mode can access U-mode pages (since we use PTE_U for RAM)
	li	t0, 1 << 18 | 1 << 8 | 1 << 5		# SUM=1, SPP=1, SPIE=1
	csrr	a1, sstatus
	or	t0, t0, a1
	csrw	sstatus, t0

	j	start_kernel		# hart 1 jump to c

other_harts_start_hart1_and_stop:
	# Other harts: Check if hart 1 is already started
	# If not, start hart 1, then stop themselves
	
	# Setup a minimal stack for SBI calls
	slli	t0, tp, 10		# shift left the hart id by 1024
	la	sp, stacks + STACK_SIZE	# set the initial stack pointer
	add	sp, sp, t0		# move to our stack space
	
	# Use SBI HSM to start hart 1
	# sbi_hart_start(hartid=1, start_addr=_start, opaque=dtb_addr)
	li	a0, 1			# hartid = 1
	la	a1, _start		# start_addr = _start
	mv	a2, a1			# opaque = dtb_addr (passed from OpenSBI)
	li	a6, 0			# fid = SBI_HSM_HART_START
	li	a7, 0x48534D		# eid = SBI_EXT_HSM
	ecall				# Make SBI call
	
	# Now stop ourselves using SBI HSM
	li	a0, 0			# No parameters needed
	li	a1, 0
	li	a2, 0
	li	a6, 1			# fid = SBI_HSM_HART_STOP
	li	a7, 0x48534D		# eid = SBI_EXT_HSM
	ecall				# Make SBI call - should not return
	
	# If we reach here, SBI HSM is not available, fall back to WFI loop
	j	park

park:
	# General park for other harts that cannot be stopped via SBI HSM
	# This is a fallback when SBI HSM is not available
	wfi				# Wait for interrupt (low power state)
	j	park			# Loop forever

# =================================================================
# Entry point for secondary harts started by our kernel
# This function will set up the environment and drop to User Mode
# =================================================================
.globl _secondary_start
_secondary_start:
    # Per the RISC-V SBI specification, when a hart is started via
    # sbi_hart_start, it begins execution at the specified start_addr
    # with the following register states:
    # a0: hartid of this hart
    # a1: the opaque value passed to sbi_hart_start
    # All other registers are undefined.

    # We use the opaque value (a1) to pass the user function's address.

    # --- Basic S-mode setup ---

    # 1. Set up tp register to point to this hart's per_cpu_data.
    la   tp, cpu_data_area
    li   t0, 16  # sizeof(struct per_cpu_data)
    mul  t1, a0, t0
    add  tp, tp, t1  # tp = &cpu_data_area[hartid]

    # 2. Set up stack pointer. This will become the user stack.
    la   sp, stacks
    li   t0, STACK_SIZE
    add  t1, a0, 1
    mul  t2, t0, t1
    add  sp, sp, t2  # sp now points to the top of this hart's stack region

    # 2.5. Initialize trap vector for this hart.
    #      This is CRITICAL. Without it, any trap (like ecall) will jump to address 0.
    call trap_init

    # --- Prepare for U-mode entry ---

    # 3. Set sepc to the user function's address, which is in a1.
    csrw sepc, a1

    # 4. Prepare sstatus for sret.
    #    - Clear SPP to 0 for U-mode.
    #    - Set SPIE to 1 to enable interrupts after sret.
    li   t0, 1 << 8      # Load mask for SPP bit (256) into a register
    csrc sstatus, t0    # Atomically clear SPP bit using the register
    li   t0, 1 << 5      # Load mask for SPIE bit (32) into a register
    csrs sstatus, t0    # Atomically set SPIE bit

    # 5. Set up arguments for the user function (hartid in a0).
    #    (No code needed, a0 is already correct)

    # 6. Switch to User Mode.
    sret

.L_secondary_hang:
    wfi
    j .L_secondary_hang

.section .bss
.balign 16
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # allocate space for all the harts stacks

# Global variables to store hart ID and DTB address passed by OpenSBI
.section .data
.align 8
.global boot_hartid_asm
boot_hartid_asm:
	.dword 0

.global dtb_addr_asm  
dtb_addr_asm:
	.dword 0

# Legacy DTB address variable (for compatibility)
.global dtb_addr
dtb_addr:
	.dword 0

	.end				# End of file
