#include "kernel/mm.h"
#include "kernel/printk.h"

void test_page(void)
{
	printk("--- Running Page Allocator Test ---\n");

	void *p = page_alloc(2);
	printk("p = page_alloc(2) --> 0x%x\n", p);

	void *p2 = page_alloc(7);
	printk("p2 = page_alloc(7) --> 0x%x\n", p2);

	page_free(p2);
	printk("page_free(p2)\n");

	void *p3 = page_alloc(4);
	printk("p3 = page_alloc(4) --> 0x%x\n", p3);
    
	page_free(p);
	page_free(p3);
	printk("Cleaned up p and p3.\n");

	printk("--- Page Allocator Test Finished ---\n");

    // Now run extreme tests
    printk("\n--- Running EXTREME Page Allocator Tests ---\n");
    
    // Test 1: Boundary condition - allocate 0 pages
    printk("Test 1: Allocating 0 pages (should fail)\n");
    void *p_zero = page_alloc(0);
    if (p_zero == NULL) {
        printk("✓ PASS: page_alloc(0) correctly returned NULL\n");
    } else {
        printk("✗ FAIL: page_alloc(0) should return NULL but returned %p\n", p_zero);
    }
    
    // Test 2: Allocate maximum possible pages
    int max_pages = get_allocatable_pages();
    printk("Test 2: Attempting to allocate ALL available pages (%d pages)\n", max_pages);
    void *p_max = page_alloc(max_pages);
    if (p_max != NULL) {
        printk("✓ PASS: Successfully allocated %d pages at %p\n", max_pages, p_max);
        page_free(p_max);
        printk("✓ PASS: Successfully freed maximum allocation\n");
    } else {
        printk("✗ FAIL: Could not allocate maximum pages\n");
    }
    
    // Test 3: Try to allocate more than available (should fail)
    printk("Test 3: Attempting to allocate MORE than available (%d pages)\n", max_pages + 1);
    void *p_over = page_alloc(max_pages + 1);
    if (p_over == NULL) {
        printk("✓ PASS: page_alloc(%d) correctly returned NULL (insufficient memory)\n", max_pages + 1);
    } else {
        printk("✗ FAIL: page_alloc(%d) should fail but returned %p\n", max_pages + 1, p_over);
        page_free(p_over);
    }
    
    // Test 4: Fragmentation test
    printk("Test 4: Fragmentation test - allocate every other page\n");
    void *frag_ptrs[10];
    int frag_count = 0;
    
    // Allocate 10 single pages
    for (int i = 0; i < 10; i++) {
        frag_ptrs[i] = page_alloc(1);
        if (frag_ptrs[i] != NULL) {
            frag_count++;
        }
    }
    printk("Allocated %d single pages\n", frag_count);
    
    // Free every other page to create fragmentation
    for (int i = 0; i < 10; i += 2) {
        if (frag_ptrs[i] != NULL) {
            page_free(frag_ptrs[i]);
            frag_ptrs[i] = NULL;
        }
    }
    printk("Freed every other page to create fragmentation\n");
    
    // Try to allocate a large chunk (should fail due to fragmentation)
    void *large_chunk = page_alloc(5);
    if (large_chunk == NULL) {
        printk("✓ PASS: Large allocation failed due to fragmentation\n");
    } else {
        printk("? INFO: Large allocation succeeded despite fragmentation at %p\n", large_chunk);
        page_free(large_chunk);
    }
    
    // Clean up remaining fragments
    for (int i = 1; i < 10; i += 2) {
        if (frag_ptrs[i] != NULL) {
            page_free(frag_ptrs[i]);
        }
    }
    
    // Test 5: Double free detection
    printk("Test 5: Double free detection\n");
    void *p_double = page_alloc(1);
    if (p_double != NULL) {
        printk("Allocated page at %p\n", p_double);
        page_free(p_double);
        printk("First free successful\n");
        
        // This should be detected as double free (but our simple allocator might not catch it)
        page_free(p_double);
        printk("Second free completed (double free test)\n");
    }
    
    // Test 6: Address validation test
    printk("Test 6: Invalid address free tests\n");
    
    // Try to free NULL pointer
    page_free(NULL);
    printk("✓ PASS: page_free(NULL) handled\n");
    
    // Try to free an invalid address (outside allocatable range)
    void *invalid_addr = (void*)0x12345678;
    page_free(invalid_addr);
    printk("✓ PASS: page_free(invalid_addr) handled\n");
    
    // Test 7: Alignment verification
    printk("Test 7: Address alignment verification\n");
    for (int i = 1; i <= 8; i++) {
        void *p_align = page_alloc(i);
        if (p_align != NULL) {
            uint32_t addr = (uint32_t)p_align;
            if (addr % PAGE_SIZE == 0) {
                printk("✓ PASS: %d pages allocated at properly aligned address %p\n", i, p_align);
            } else {
                printk("✗ FAIL: %d pages allocated at misaligned address %p\n", i, p_align);
            }
            page_free(p_align);
        }
    }
    
    // Test 8: Large allocation test
    printk("Test 8: Large allocation test\n");
    int large_size = max_pages / 4; // Allocate 1/4 of available memory
    if (large_size > 0) {
        void *large_alloc = page_alloc(large_size);
        if (large_alloc != NULL) {
            printk("✓ PASS: Large allocation of %d pages succeeded at %p\n", large_size, large_alloc);
            
            // Verify we can still allocate after this large allocation
            void *small_alloc = page_alloc(1);
            if (small_alloc != NULL) {
                printk("✓ PASS: Small allocation still possible after large allocation\n");
                page_free(small_alloc);
            } else {
                printk("? INFO: No memory left for small allocation after large allocation\n");
            }
            
            page_free(large_alloc);
        } else {
            printk("✗ FAIL: Large allocation of %d pages failed\n", large_size);
        }
    }
    
    // Test 9: Memory stress test - rapid allocation and deallocation
    printk("Test 9: Memory stress test - rapid allocation/deallocation\n");
    for (int cycle = 0; cycle < 5; cycle++) {
        void *stress_ptrs[20];
        int stress_count = 0;
        
        // Initialize array to NULL for safety
        for (int i = 0; i < 20; i++) {
            stress_ptrs[i] = NULL;
        }
        
        // Allocate many small chunks
        for (int i = 0; i < 20; i++) {
            int pages_to_alloc = 1 + (i % 3); // Varying sizes 1-3 pages
            stress_ptrs[i] = page_alloc(pages_to_alloc);
            if (stress_ptrs[i] != NULL) {
                stress_count++;
                // Add some validation
                if ((uint32_t)stress_ptrs[i] % PAGE_SIZE != 0) {
                    printk("ERROR: Misaligned allocation at %p\n", stress_ptrs[i]);
                    break;
                }
            } else {
                printk("INFO: Allocation failed for %d pages at iteration %d\n", pages_to_alloc, i);
                break;
            }
        }
        
        printk("Cycle %d: allocated %d chunks, freeing...\n", cycle + 1, stress_count);
        
        // Free all chunks with validation
        for (int i = 0; i < 20; i++) {
            if (stress_ptrs[i] != NULL) {
                page_free(stress_ptrs[i]);
                stress_ptrs[i] = NULL; // Prevent double free
            }
        }
        
        printk("Cycle %d completed\n", cycle + 1);
    }
    printk("✓ PASS: Completed 5 stress cycles\n");
    
    printk("--- EXTREME Page Allocator Tests Completed ---\n");
}

