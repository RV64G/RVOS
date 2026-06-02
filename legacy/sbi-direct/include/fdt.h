#ifndef __FDT_H__
#define __FDT_H__

#include "kernel/types.h"

/* FDT header magic number */
#define FDT_MAGIC 0xd00dfeed

/* FDT node tokens */
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

/* FDT header structure */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

/* FDT property structure */
struct fdt_property {
    uint32_t len;
    uint32_t nameoff;
};

/* Boot information structure for multi-core */
struct boot_info {
    uint64_t hartid;
    uint64_t dtb_addr;
    uint64_t memory_start;
    uint64_t memory_size;
    uint32_t cpu_count;
    uint64_t timebase_freq;
};

/* Device information cache */
struct device_info {
    char name[32];
    uint64_t base_addr;
    uint64_t size;
    uint32_t irq;
    char compatible[64];
};

/* FDT cache structure */
struct fdt_cache {
    struct device_info uart;
    struct device_info plic;
    struct device_info clint;
    struct device_info *cpus;     /* Array of CPU nodes */
    struct device_info *memory;   /* Array of memory nodes */
    int cpu_count;
    int memory_count;
};

/* Global boot information */
extern struct boot_info g_boot_info;
extern struct fdt_cache g_fdt_cache;

/* FDT parsing functions */
int fdt_check_header(void *fdt);
void *fdt_get_property(void *fdt, int nodeoffset, const char *name, int *lenp);
int fdt_path_offset(void *fdt, const char *path);
const char *fdt_get_name(void *fdt, int nodeoffset, int *lenp);
uint64_t fdt_read_number(const void *cell, int size);

/* Boot and device initialization */
void boot_info_init(void);
void fdt_cache_init(void);

/* Device query functions - use cached data */
uint64_t fdt_get_uart_base(void);
uint32_t fdt_get_uart_irq(void);
uint64_t fdt_get_plic_base(void);
uint64_t fdt_get_clint_base(void);
int fdt_get_cpu_count(void);
uint64_t fdt_get_timebase_freq(void);

/* Rust interface functions */
uint64_t c_get_uart_base(void);
uint64_t c_get_plic_base(void);
uint64_t c_get_clint_base(void);

#endif /* __FDT_H__ */
