#include "kernel.h"

// External references to assembly variables
extern uint64_t boot_hartid_asm;
extern uint64_t dtb_addr_asm;

// Get the hart ID passed by OpenSBI at boot time
uint64_t get_boot_hartid(void)
{
    return boot_hartid_asm;
}

// Get the DTB address passed by OpenSBI at boot time  
uint64_t get_dtb_addr(void)
{
    return dtb_addr_asm;
}

// Compatibility function - replaces r_mhartid() for saved hart ID
uint64_t get_saved_hartid(void)
{
    return boot_hartid_asm;
}
