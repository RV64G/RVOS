/* RVOS 链接脚本。 */

/*
 * 指定输出文件的目标架构。
 * 这里的 "riscv" 同时覆盖 32 位和 64 位 RISC-V，具体 ISA 和 ABI 由编译参数
 * -march/-mabi 决定。
 */
OUTPUT_ARCH( "riscv" )

/*
 * 指定内核入口符号。
 * _start 定义在 arch/riscv/start.S 中，OpenSBI/QEMU 跳入内核后会从这里开始执行。
 */
ENTRY( _start )

/*
 * 描述内核可使用的物理内存区域。
 *
 * MEMORY 语法：
 * MEMORY
 * {
 *     name [(attr)] : ORIGIN = 起始地址, LENGTH = 长度
 * }
 *
 * 这里只定义一个名为 ram 的区域。起始地址放在 0x80200000，是为了给 OpenSBI
 * 预留 0x80000000-0x801fffff 一段常见占用空间。
 *
 * 属性保持为简单的 rwx，GNU ld 和 lld 都能接受。更细的运行期权限由页表控制。
 */
MEMORY
{
	ram   (rwx) : ORIGIN = 0x80200000, LENGTH = 126M
}

/*
 * 描述输入段到输出段的映射，以及各段在内存中的摆放顺序。
 *
 * SECTIONS 语法：
 * SECTIONS
 * {
 *     段描述或符号赋值
 * }
 *
 * PROVIDE 用来导出链接期符号，例如 _text_start、_bss_end。C/汇编代码通过这些
 * 符号定位内核镜像边界、清零 BSS、初始化页分配器。
 *
 * '.' 表示当前链接地址，随着段内容写入不断向后移动。
 */
SECTIONS
{
	/*
	 * 汇总代码段。
	 * 先放内核代码，再把用户态测试代码放进独立的页对齐区间，方便页表按区间设置
	 * 用户可执行权限。
	 */
	.text : {
		PROVIDE(_text_start = .);
		/* 内核代码，不包含 build/user 下的对象文件。 */
		*(EXCLUDE_FILE(*build/user/*.o) .text .text.*)
		
		/* 用户态代码区间，用于当前阶段的权限控制。 */
		. = ALIGN(4096);
		PROVIDE(_user_text_start = .);
		*(.user_text)
		/* 收集 user/ 目录编译出的普通 .text 段。 */
		*build/user/*.o(.text .text.*)
		. = ALIGN(4096);
		PROVIDE(_user_text_end = .);
		PROVIDE(_text_end = .);
	} >ram

	.rodata : {
		PROVIDE(_rodata_start = .);
		*(.rodata .rodata.*)
		PROVIDE(_rodata_end = .);
	} >ram

	.data : {
		/*
		 * 数据段按页对齐，便于后续按页设置权限和映射。
		 */
		. = ALIGN(4096);
		PROVIDE(_data_start = .);
		/*
		 * 当前内核不区分 sdata 和 data，统一放入 .data。
		 */
		*(.sdata .sdata.*)
		*(.data .data.*)
		PROVIDE(_data_end = .);
	} >ram

	.bss :{
		/*
		 * COMMON 符号通常也需要进入 BSS，启动阶段由 start.S 统一清零。
		 */
		PROVIDE(_bss_start = .);
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		PROVIDE(_bss_end = .);
	} >ram

	PROVIDE(_memory_start = ORIGIN(ram));
	PROVIDE(_memory_end = ORIGIN(ram) + LENGTH(ram));

	PROVIDE(_heap_start = _bss_end);
	PROVIDE(_heap_size = _memory_end - _heap_start);
}
