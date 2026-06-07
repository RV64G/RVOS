#ifndef __BOOT_INFO_H__
#define __BOOT_INFO_H__

#include "kernel/types.h"

// Get the hart ID passed by OpenSBI at boot time
uint64_t get_boot_hartid(void);

// Get the DTB address passed by OpenSBI at boot time  
uint64_t get_dtb_addr(void);

// Compatibility function - replaces r_mhartid() for saved hart ID
uint64_t get_saved_hartid(void);

// Macro to replace r_mhartid() calls
#define r_saved_hartid() get_saved_hartid()

#endif /* __BOOT_INFO_H__ */
