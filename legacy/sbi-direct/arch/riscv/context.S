# This Code derived from xv6-riscv (64bit)
# -- https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/swtch.S

# ============ MACRO ==================
.macro ctx_save base
        sd ra, 0(\base)
        sd sp, 8(\base)
        sd s0, 16(\base)
        sd s1, 24(\base)
        sd s2, 32(\base)
        sd s3, 40(\base)
        sd s4, 48(\base)
        sd s5, 56(\base)
        sd s6, 64(\base)
        sd s7, 72(\base)
        sd s8, 80(\base)
        sd s9, 88(\base)
        sd s10, 96(\base)
        sd s11, 104(\base)
	sd a0, 112(\base)
	sd a7, 120(\base)
.endm

.macro ctx_load base
        ld ra, 0(\base)
        ld sp, 8(\base)
        ld s0, 16(\base)
        ld s1, 24(\base)
        ld s2, 32(\base)
        ld s3, 40(\base)
        ld s4, 48(\base)
        ld s5, 56(\base)
        ld s6, 64(\base)
        ld s7, 72(\base)
        ld s8, 80(\base)
        ld s9, 88(\base)
        ld s10, 96(\base)
        ld s11, 104(\base)
        ld a0, 112(\base)
        ld a7, 120(\base)
.endm

.macro reg_save base
        # save the registers.
        sd ra, 0(\base)
        sd sp, 8(\base)
        sd gp, 16(\base)
        sd tp, 24(\base)
        sd t0, 32(\base)
        sd t1, 40(\base)
        sd t2, 48(\base)
        sd s0, 56(\base)
        sd s1, 64(\base)
        sd a0, 72(\base)
        sd a1, 80(\base)
        sd a2, 88(\base)
        sd a3, 96(\base)
        sd a4, 104(\base)
        sd a5, 112(\base)
        sd a6, 120(\base)
        sd a7, 128(\base)
        sd s2, 136(\base)
        sd s3, 144(\base)
        sd s4, 152(\base)
        sd s5, 160(\base)
        sd s6, 168(\base)
        sd s7, 176(\base)
        sd s8, 184(\base)
        sd s9, 192(\base)
        sd s10, 200(\base)
        sd s11, 208(\base)
        sd t3, 216(\base)
        sd t4, 224(\base)
        sd t5, 232(\base)
.endm

.macro reg_load base
        # restore registers.
        ld ra, 0(\base)
        ld sp, 8(\base)
        ld gp, 16(\base)
        ld tp, 24(\base)
        ld t0, 32(\base)
        ld t1, 40(\base)
        ld t2, 48(\base)
        ld s0, 56(\base)
        ld s1, 64(\base)
        ld a0, 72(\base)
        ld a1, 80(\base)
        ld a2, 88(\base)
        ld a3, 96(\base)
        ld a4, 104(\base)
        ld a5, 112(\base)
        ld a6, 120(\base)
        ld a7, 128(\base)
        ld s2, 136(\base)
        ld s3, 144(\base)
        ld s4, 152(\base)
        ld s5, 160(\base)
        ld s6, 168(\base)
        ld s7, 176(\base)
        ld s8, 184(\base)
        ld s9, 192(\base)
        ld s10, 200(\base)
        ld s11, 208(\base)
        ld t3, 216(\base)
        ld t4, 224(\base)
        ld t5, 232(\base)
        ld t6, 240(\base)
.endm
# ============ Macro END   ==================
 
# Context switch
#
#   void sys_switch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.

.text

.globl sys_switch
.align 4
sys_switch:

        #ctx_save a0  # a0 => struct context *old
        ctx_load a0  # a1 => struct context *new
        
	mret          # pc=ra; swtch to new task (new->ra)

.globl atomic_swap
.align 4
atomic_swap:
        li a5, 1
        amoswap.w.aq a5, a5, 0(a0)
        mv a0, a5
        ret

.globl trap_vector
# the trap vector base address must always be aligned on a 4-byte boundary
.align 4
trap_vector:	# save context(registers).
	csrrw	t6, sscratch, t6	# swap t6 and sscratch (use sscratch instead of mscratch)
        reg_save t6
        
        # Manually save the original t6 (which is now in sscratch)
        # We can use t0 as temp because it has already been saved by reg_save
        csrr    t0, sscratch
        sd      t0, 240(t6)     # Save original t6 to context (offset 240)

        csrw	sscratch, t6

	# save sepc to context of current task (S-mode equivalent of mepc)
	csrr	a0, sepc
	sd	a0, 248(t6)		# offset for 64-bit: 31 * 8 = 248

	# call the C trap handler in trap.c
	csrr	a0, sepc
	csrr	a1, scause
	csrr	a2, sscratch
	call	trap_handler
	# trap_handler will return the return address via a0.
	csrw	sepc, a0

	# load context(registers).
	csrr	t6, sscratch
	reg_load t6
	sret				# Use sret instead of mret for S-mode

# void switch_to(struct context *next);
# a0: pointer to the context of the next task
.globl switch_to
.align 4
switch_to:
	# switch sscratch to point to the context of the next task
	csrw	sscratch, a0
	# set sepc to the pc of the next task
	ld	a1, 248(a0)		# offset for 64-bit: 31 * 8 = 248
	csrw	sepc, a1

        ld      a1, 256(a0)     # 加载调度器设置的sstatus (offset: 32 * 8 = 256)
        csrw    sstatus, a1     # 使用 sstatus 而不是 mstatus
	# Restore all GP registers
	# Use t6 to point to the context of the new task
	mv	t6, a0
	reg_load t6

	# Do actual context switching.
	# Notice this will enable global interrupt
	sret				# Use sret instead of mret for S-mode
.end
