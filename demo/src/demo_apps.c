#include "demo_apps.h"
#include "app/app.h"
#include "translator.h"
#include "virtual_space/vpage.h"

#include <stdint.h>
#include <zephyr/sys/printk.h>

void demo_oob_entry(void *p1, void *p2, void *p3)
{
    app *current_app = app_current();

    printk("[demo] OOB: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(64);
    if(!buf)
    {
        printk("[demo] OOB: vmmu_malloc failed\n");
        return;
    }

    printk("[demo] OOB: allocated buf at %p\n", buf);

    void *oob = (void *)current_app->heap_limit;
    printk("[demo] OOB: writing to guard page at %p — expect FAULT\n", oob);
    vmmu_check(oob, 4, PERM_WRITE);

    printk("[demo] OOB: ERROR — should not reach here\n");
}

void demo_victim_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] VICTIM: app %u alive — sleeping\n", (unsigned)current_app->id);

    while(1)
    {
        k_msleep(500);
    }
}

void demo_crossapp_entry(void *p1, void *p2, void *p3)
{
    app *current_app = app_current();

    printk("[demo] CROSS: app %u running\n", (unsigned)current_app->id);

    void *cross_ptr = (void *)(9u * PAGE_SIZE);
    printk("[demo] CROSS: reading from app 1's range at %p — expect FAULT\n",
           cross_ptr);
    vmmu_check(cross_ptr, 4, PERM_READ);

    printk("[demo] CROSS: ERROR — should not reach here\n");
}

void demo_demand_entry(void *p1, void *p2, void *p3)
{
    app *current_app = app_current();

    printk("[demo] DEMAND: app %u running (demand paging enabled)\n",
           (unsigned)current_app->id);

    void *ptr = (void *)current_app->heap_base;
    printk("[demo] DEMAND: accessing unallocated page at %p\n", ptr);
    vmmu_check(ptr, 4, PERM_WRITE);

    printk("[demo] DEMAND: demand fault handled — OS mapped the frame on first access\n");

    void *ptr2 = (void *)(current_app->heap_base + PAGE_SIZE);
    printk("[demo] DEMAND: accessing second unallocated page at %p\n", ptr2);
    vmmu_check(ptr2, 4, PERM_WRITE);

    printk("[demo] DEMAND: second demand fault handled — app %u exiting normally\n",
           (unsigned)current_app->id);
}

void demo_subpage_oob_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] SUBPAGE: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(20);
    if(!buf)
    {
        printk("[demo] SUBPAGE: vmmu_malloc failed\n");
        return;
    }

    printk("[demo] SUBPAGE: allocated 20-byte buf at %p\n", buf);

    void *oob = (uint8_t *)buf + 22;
    printk("[demo] SUBPAGE: accessing buf+22 at %p — expect SHADOW FAULT\n", oob);
    vmmu_check(oob, 4, PERM_WRITE);

    printk("[demo] SUBPAGE: ERROR — should not reach here\n");
}

static volatile int g_demo_global = 0;

void demo_globals_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] GLOBALS: app %u running\n", (unsigned)current_app->id);

    if(__vmmu_globals_size == 0 || current_app->globals_frame == 0)
    {
        printk("[demo] GLOBALS: GlobalsPass not active — globals frame not allocated\n");
        printk("[demo] GLOBALS: build with -fpass-plugin=libGlobalsPass.so to enable\n");
        printk("[demo] GLOBALS: SKIP\n");
        return;
    }

    printk("[demo] GLOBALS: globals frame vaddr=0x%08x size=%u bytes\n",
           (unsigned)current_app->globals_frame, (unsigned)__vmmu_globals_size);

    g_demo_global = 42;
    printk("[demo] GLOBALS: g_demo_global = %d (expect 42)\n",
           (int)g_demo_global);

    void *frame_ptr = (void *)(uintptr_t)current_app->globals_frame;
    vmmu_check(frame_ptr, 4, PERM_READ);
    printk("[demo] GLOBALS: in-bounds shadow check on globals frame PASSED\n");

    void *oob = (uint8_t *)frame_ptr + __vmmu_globals_size;
    printk("[demo] GLOBALS: accessing past frame end at %p — expect SHADOW FAULT\n",
           oob);
    vmmu_check(oob, 4, PERM_WRITE);

    printk("[demo] GLOBALS: ERROR — should not reach here\n");
}

static void stack_recurse(int depth)
{
    volatile uint8_t frame[128];
    frame[0] = (uint8_t)depth;

    vmmu_stack_check();

    printk("[demo] STACKOVERFLOW: depth=%d sp consuming stack...\n", depth);
    stack_recurse(depth + 1);

    frame[1]++;
}

void demo_stackoverflow_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] STACKOVERFLOW: app %u running — recursing until overflow\n",
           (unsigned)current_app->id);

    stack_recurse(0);

    printk("[demo] STACKOVERFLOW: ERROR — should not reach here\n");
}
