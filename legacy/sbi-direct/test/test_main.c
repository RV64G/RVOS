#include "kernel/printk.h"
#include "test.h"

void test_main(void) {
    printk("========= RUNNING ALL TESTS =========\n\n");
    
    test_user_multicore_start();
    test_page();
    
    printk("\n========= ALL TESTS PASSED =========\n");
    printk("System halted for test verification.\n");
    while(1) {}
}

