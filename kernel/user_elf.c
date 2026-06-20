#include "user_elf.h"

#include "align.h"
#include "page_alloc.h"
#include "printk.h"
#include "string.h"

#define EI_NIDENT 16

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_EXEC 2
#define EM_RISCV 243

#define PT_LOAD 1

struct elf64_header
{
    unsigned char ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf64_program_header
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static int range_fits(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int validate_elf_header(const struct elf64_header *header,
                               uint64_t elf_size)
{
    if (elf_size < sizeof(*header))
    {
        return 0;
    }

    if (header->ident[0] != 0x7f || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F')
    {
        return 0;
    }

    if (header->ident[4] != ELFCLASS64 || header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != EV_CURRENT)
    {
        return 0;
    }

    if (header->type != ET_EXEC || header->machine != EM_RISCV ||
        header->version != EV_CURRENT)
    {
        return 0;
    }

    if (header->phentsize != sizeof(struct elf64_program_header) ||
        header->phnum == 0)
    {
        return 0;
    }

    return range_fits(header->phoff,
                      (uint64_t)header->phnum * header->phentsize,
                      elf_size);
}

static int collect_load_span(const struct elf64_header *header,
                             const void *elf, uint64_t elf_size,
                             uint64_t *span_start, uint64_t *span_end)
{
    const char *base = (const char *)elf;
    uint64_t min_va = UINT64_MAX;
    uint64_t max_va = 0;
    int found = 0;

    for (uint16_t i = 0; i < header->phnum; i++)
    {
        const struct elf64_program_header *ph =
            (const struct elf64_program_header *)(const void *)(base +
                header->phoff + (uint64_t)i * header->phentsize);

        if (ph->type != PT_LOAD)
        {
            continue;
        }

        if (ph->memsz < ph->filesz ||
            !range_fits(ph->offset, ph->filesz, elf_size))
        {
            return 0;
        }

        uint64_t start = align_down(ph->vaddr, VM_PAGE_SIZE);
        uint64_t end = align_up(ph->vaddr + ph->memsz, VM_PAGE_SIZE);
        if (end < start)
        {
            return 0;
        }

        if (start < min_va)
        {
            min_va = start;
        }
        if (end > max_va)
        {
            max_va = end;
        }
        found = 1;
    }

    if (!found || max_va <= min_va)
    {
        return 0;
    }

    *span_start = min_va;
    *span_end = max_va;
    return 1;
}

static int copy_load_segments(const struct elf64_header *header,
                              const void *elf, uint64_t span_start,
                              void *span_phys)
{
    const char *base = (const char *)elf;
    char *dst_base = (char *)span_phys;

    for (uint16_t i = 0; i < header->phnum; i++)
    {
        const struct elf64_program_header *ph =
            (const struct elf64_program_header *)(const void *)(base +
                header->phoff + (uint64_t)i * header->phentsize);

        if (ph->type != PT_LOAD)
        {
            continue;
        }

        memcpy(dst_base + (ph->vaddr - span_start), base + ph->offset,
               ph->filesz);
    }

    return 1;
}

int user_elf_load(struct vm_space *space, const void *elf, uint64_t elf_size,
                  uint64_t *entry)
{
    const struct elf64_header *header = (const struct elf64_header *)elf;
    uint64_t span_start;
    uint64_t span_end;
    uint64_t span_size;
    uint64_t pages;
    void *phys;

    if (!space || !elf || !entry || !validate_elf_header(header, elf_size))
    {
        printk("User ELF rejected\r\n");
        return 0;
    }

    if (!collect_load_span(header, elf, elf_size, &span_start, &span_end))
    {
        printk("User ELF load span invalid\r\n");
        return 0;
    }

    if (header->entry < span_start || header->entry >= span_end)
    {
        printk("User ELF entry outside load span\r\n");
        return 0;
    }

    span_size = span_end - span_start;
    pages = span_size / VM_PAGE_SIZE;
    phys = phys_alloc_pages(pages);
    if (!phys)
    {
        printk("User ELF page allocation failed\r\n");
        return 0;
    }

    /*
     * 第一版 loader 先把所有 PT_LOAD 覆盖范围合成一个连续映射，并给 U-mode
     * RWX 权限。这样能先验证 ELF header/program header/entry 路径；按段权限拆页
     * 会在后续替换这里。
     */
    memzero(phys, span_size);
    if (!copy_load_segments(header, elf, span_start, phys))
    {
        phys_free_pages(phys, pages);
        return 0;
    }

    if (!vm_map_range(space, span_start, (uint64_t)(uintptr_t)phys, span_size,
                      VM_MAP_READ | VM_MAP_WRITE | VM_MAP_EXEC | VM_MAP_USER))
    {
        printk("User ELF mapping failed\r\n");
        phys_free_pages(phys, pages);
        return 0;
    }

    *entry = header->entry;
    return 1;
}
