#include "demo_apps.h"
#include "app/app.h"
#include "boundary.h"
#include "tlb.h"
#include "translator.h"
#include "virtual_space/vpage.h"
#include "vmmu.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

K_MSGQ_DEFINE(g_ipc_msgq, 32, 4, 4);

#define BLINK_COUNT 10
#define BLINK_PERIOD_MS 200
#define BLINK_STACK_SIZE 512

K_THREAD_STACK_DEFINE(g_blink_tick_stack, BLINK_STACK_SIZE);
static struct k_thread g_blink_tick_thread;
static struct k_sem g_blink_sem;

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

void demo_tlb_hit_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] TLB-HIT: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(64);
    if(!buf)
    {
        printk("[demo] TLB-HIT: vmmu_malloc failed\n");
        return;
    }
    printk("[demo] TLB-HIT: allocated 64 bytes at %p\n", buf);

    tlb_reset_stats();

    for(int i = 0; i < 10; i++)
    {
        vmmu_check((uint8_t *)buf + i, 1, PERM_READ);
    }

    printk("[demo] TLB-HIT: tlb_hits=%u (expect 10)\n",
           (unsigned)tlb_hits);

    vmmu_free(buf);
}

void demo_tlb_uaf_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] TLB-UAF: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(64);
    if(!buf)
    {
        printk("[demo] TLB-UAF: vmmu_malloc failed\n");
        return;
    }

    tlb_reset_stats();

    vmmu_check(buf, 1, PERM_READ);
    printk("[demo] TLB-UAF: hits before free = %u (expect 1)\n",
           (unsigned)tlb_hits);

    vmmu_free(buf);

    uint32_t hits_snapshot = tlb_hits;

    printk("[demo] TLB-UAF: accessing freed buffer — expect SHADOW FAULT\n");
    vmmu_check(buf, 1, PERM_READ);

    printk("[demo] TLB-UAF: hits after free = %u (should still be %u — no new hit)\n",
           (unsigned)tlb_hits, (unsigned)hits_snapshot);
    printk("[demo] TLB-UAF: ERROR — should not reach here\n");
}

void demo_tlb_range_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] TLB-RANGE: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(5000);
    if(!buf)
    {
        printk("[demo] TLB-RANGE: vmmu_malloc failed (need 2 frames)\n");
        return;
    }
    printk("[demo] TLB-RANGE: allocated 5000 bytes at %p (spans 2 pages)\n",
           buf);

    tlb_reset_stats();

    vmmu_check((uint8_t *)buf + 0, 1, PERM_READ);
    vmmu_check((uint8_t *)buf + 4095, 1, PERM_READ);
    vmmu_check((uint8_t *)buf + 4096, 1, PERM_READ);
    vmmu_check((uint8_t *)buf + 4999, 1, PERM_READ);

    printk("[demo] TLB-RANGE: tlb_hits=%u (expect 4 — both pages covered)\n",
           (unsigned)tlb_hits);

    vmmu_free(buf);
}

void demo_ipc_valid_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] IPC-VALID: app %u running\n", (unsigned)current_app->id);

    uint8_t *msg = vmmu_malloc(32);
    if(!msg)
    {
        printk("[demo] IPC-VALID: vmmu_malloc failed\n");
        return;
    }
    for(int i = 0; i < 32; i++)
    {
        msg[i] = (uint8_t)i;
    }

    int r = vmmu_msgq_put(&g_ipc_msgq,
                          (vpointer)(uintptr_t)msg,
                          K_NO_WAIT);
    printk("[demo] IPC-VALID: vmmu_msgq_put returned %d (expect 0)\n", r);

    k_msgq_purge(&g_ipc_msgq);

    vmmu_free(msg);
    printk("[demo] IPC-VALID: app %u exiting normally\n", (unsigned)current_app->id);
}

void demo_ipc_invalid_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] IPC-INVALID: app %u running\n", (unsigned)current_app->id);

    vpointer bad = (vpointer)current_app->heap_limit;
    printk("[demo] IPC-INVALID: sending guard page %p — expect FAULT\n",
           (void *)bad);
    vmmu_msgq_put(&g_ipc_msgq, bad, K_NO_WAIT);

    printk("[demo] IPC-INVALID: ERROR — should not reach here\n");
}

void demo_ipc_crossapp_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] IPC-CROSS: app %u running\n", (unsigned)current_app->id);

    vpointer cross = (vpointer)(9u * PAGE_SIZE);
    printk("[demo] IPC-CROSS: sending app 1's address %p — expect FAULT\n",
           (void *)cross);
    vmmu_msgq_put(&g_ipc_msgq, cross, K_NO_WAIT);

    printk("[demo] IPC-CROSS: ERROR — should not reach here\n");
}

static void blink_tick_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for(int i = 0; i < BLINK_COUNT; i++)
    {
        k_msleep(BLINK_PERIOD_MS);
        k_sem_give(&g_blink_sem);
    }
}

void demo_semaphore_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] SEM: app %u running — virtual LED blinker\n",
           (unsigned)current_app->id);

    k_sem_init(&g_blink_sem, 0, 1);

    k_thread_create(&g_blink_tick_thread,
                    g_blink_tick_stack,
                    BLINK_STACK_SIZE,
                    blink_tick_fn,
                    NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    int led_state = 0;
    for(int i = 0; i < BLINK_COUNT; i++)
    {
        k_sem_take(&g_blink_sem, K_FOREVER);
        led_state ^= 1;
        printk("[demo] SEM: [tick %2d] LED %s\n",
               i + 1, led_state ? "ON " : "OFF");
    }

    printk("[demo] SEM: %d blinks completed — app %u exiting\n",
           BLINK_COUNT, (unsigned)current_app->id);
}

#define TLB_TEST_ACCESSES 100

void demo_tlb_verify_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    app *current_app = app_current();
    printk("[demo] TLB-VERIFY: app %u running\n", (unsigned)current_app->id);

    void *buf = vmmu_malloc(256);
    if(!buf)
    {
        printk("[demo] TLB-VERIFY: vmmu_malloc failed\n");
        return;
    }

    tlb_reset_stats();
    for(int i = 0; i < TLB_TEST_ACCESSES; i++)
    {
        vmmu_check((uint8_t *)buf + (i % 256), 1, PERM_READ);
    }
    uint32_t hits_live = tlb_hits;
    printk("[demo] TLB-VERIFY: %d accese -> tlb_hits=%u (asteptat %d)\n",
           TLB_TEST_ACCESSES, (unsigned)hits_live, TLB_TEST_ACCESSES);

    vmmu_free(buf);

    int after_free = tlb_lookup((uint32_t)(uintptr_t)buf, 1, (uint8_t)current_app->id);
    printk("[demo] TLB-VERIFY: lookup dupa free = %s (asteptat MISS)\n",
           (after_free == 0) ? "HIT" : "MISS");

    printk("[demo] TLB-VERIFY: app %u exiting normally\n", (unsigned)current_app->id);
}
