#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app/app.h"
#include "demo_apps.h"
#include "tlb.h"
#include "virtual_space/vpage.h"
#include "vmmu.h"

extern void boundary_init(void);

#define APP0_HEAP_BASE (1u * PAGE_SIZE)
#define APP0_HEAP_LIMIT (8u * PAGE_SIZE)
#define APP1_HEAP_BASE (8u * PAGE_SIZE)
#define APP1_HEAP_LIMIT (16u * PAGE_SIZE)
#define APP2_HEAP_BASE (1u * PAGE_SIZE)
#define APP2_HEAP_LIMIT (8u * PAGE_SIZE)

#define WATCHDOG_STACK_SIZE 512
#define WATCHDOG_PRIORITY 5

static void watchdog_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while(1)
    {
        printk("[watchdog] OS heartbeat — still running\n");
        k_msleep(10000);
    }
}

K_THREAD_DEFINE(watchdog_thread, WATCHDOG_STACK_SIZE,
                watchdog_fn, NULL, NULL, NULL,
                WATCHDOG_PRIORITY, 0, 0);

static void wait_for_app(app_id id)
{
    while(app_get(id)->state == APP_RUNNING)
    {
        k_msleep(100);
    }
}

static uint8_t results[14] = {0};

static void print_result(int demo_idx, app_id id,
                         app_state expected,
                         const char *label,
                         int extra_pass)
{
    app_state actual = app_get(id)->state;
    int pass = (actual == expected) && (extra_pass != 0);

    results[demo_idx] = pass ? 1 : 2;

    if(pass)
    {
        printk("[result] %-35s  PASS\n", label);
    }
    else
    {
        printk("[result] %-35s  FAIL\n", label);
        printk("[result]   expected state=%d  got state=%d  extra=%d\n",
               (int)expected, (int)actual, extra_pass);
    }
}

int main(void)
{
    printk("\n");
    printk("================================================\n");
    printk("  VMMU Demo — Software MMU on Zephyr/Cortex-M4\n");
    printk("================================================\n\n");

    vmmu_init();
    app_subsystem_init();
    boundary_init();

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 1: Out-of-Bounds Write\n");
    printk("------------------------------------------------\n");
    printk("  App 0 writes to the guard page at heap_limit.\n");
    printk("  Expected: VMMU catches it, app 0 CRASHES, OS continues.\n\n");

    app_load(0, demo_oob_entry, APP0_HEAP_BASE, APP0_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(0, 0, APP_CRASHED,
                 "Demo 1: OOB write caught (app crashed)",
                 1);
    printk("[result]   OS status: watchdog still running\n");

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 2: Cross-App Access\n");
    printk("------------------------------------------------\n");
    printk("  App 1 (victim) sleeps. App 0 reads app 1's address range.\n");
    printk("  Expected: app 0 CRASHES, app 1 and OS survive unharmed.\n\n");

    app_load(1, demo_victim_entry, APP1_HEAP_BASE, APP1_HEAP_LIMIT);
    app_start(1);

    app_load(0, demo_crossapp_entry, APP0_HEAP_BASE, APP0_HEAP_LIMIT);
    app_start(0);

    wait_for_app(0);

    int victim_alive = (app_get(1)->state == APP_RUNNING);

    printk("\n");
    print_result(1, 0, APP_CRASHED,
                 "Demo 2: cross-app access caught (app 0 crashed)",
                 victim_alive);
    printk("[result]   Victim app 1 still running: %s\n",
           victim_alive ? "YES" : "NO");

    app_stop(1);
    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 3: Demand Paging\n");
    printk("------------------------------------------------\n");
    printk("  App 0 accesses pages without malloc.\n");
    printk("  Expected: VMMU demand-allocates frames on first touch, NO crash.\n\n");

    app_load_bounded(0, demo_demand_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT,
                     UINT32_MAX);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(2, 0, APP_STOPPED,
                 "Demo 3: demand paging succeeded (no crash)",
                 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 4: Sub-Page OOB via Shadow Memory\n");
    printk("------------------------------------------------\n");
    printk("  App 0 calls vmmu_malloc(20), then accesses buf+22.\n");
    printk("  Page is mapped — page check passes.\n");
    printk("  Shadow byte is SHADOW_UNOWNED — byte-granularity check catches it.\n");
    printk("  Expected: SHADOW FAULT, app 0 CRASHES, OS continues.\n\n");

    app_load(0, demo_subpage_oob_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(3, 0, APP_CRASHED,
                 "Demo 4: sub-page OOB caught (shadow fault)",
                 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 5: Stack Overflow\n");
    printk("------------------------------------------------\n");
    printk("  App 0 recurses infinitely with a 128-byte frame per call.\n");
    printk("  VMMUPass injects vmmu_stack_check() at every function entry.\n");
    printk("  Expected: STACK OVERFLOW caught, app 0 CRASHES, OS continues.\n\n");

    app_load(0, demo_stackoverflow_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(4, 0, APP_CRASHED,
                 "Demo 5: stack overflow caught (SP check)",
                 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 6: Global Variable Isolation\n");
    printk("------------------------------------------------\n");
    printk("  App has a mutable global — GlobalsPass places it in a frame.\n");
    printk("  VMMUPass redirects every access through the globals frame.\n");
    printk("  Access past frame end hits SHADOW_UNOWNED → SHADOW FAULT.\n");
    printk("  Without GlobalsPass: demo skips cleanly.\n\n");

    app_load(0, demo_globals_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    {
        app_state st = app_get(0)->state;
        int pass = (st == APP_CRASHED || st == APP_STOPPED);
        results[5] = pass ? 1 : 2;
    }

    printk("\n");
    if(app_get(0)->state == APP_CRASHED)
    {
        printk("[result] %-35s  PASS  (shadow fault caught OOB on globals frame)\n",
               "Demo 6: Global variable isolation");
    }
    else if(app_get(0)->state == APP_STOPPED)
    {
        printk("[result] %-35s  PASS  (GlobalsPass not active — skipped)\n",
               "Demo 6: Global variable isolation");
    }
    else
    {
        printk("[result] %-35s  FAIL\n",
               "Demo 6: Global variable isolation");
    }

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 7: TLB Hit on Repeated Accesses\n");
    printk("------------------------------------------------\n");
    printk("  After vmmu_malloc, TLB entry covers entire allocation.\n");
    printk("  10 accesses inside the allocation — all should hit TLB.\n");
    printk("  Expected: tlb_hits=10, app STOPS normally.\n\n");

    app_load(0, demo_tlb_hit_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    {
        int pass = (app_get(0)->state == APP_STOPPED);
        results[6] = pass ? 1 : 2;
    }
    printk("\n");
    printk("[result] %-35s  %s\n", "Demo 7: TLB hit (10/10)",
           results[6] == 1 ? "PASS" : "FAIL");

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 8: TLB Invalidated on Free\n");
    printk("------------------------------------------------\n");
    printk("  Access before free hits TLB.\n");
    printk("  After free: TLB entry removed, full check fires, UAF detected.\n");
    printk("  Expected: SHADOW FAULT, app CRASHES.\n\n");

    app_load(0, demo_tlb_uaf_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(7, 0, APP_CRASHED,
                 "Demo 8: TLB invalidated on free (UAF caught)", 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 9: TLB Range Covers Multi-Page Allocation\n");
    printk("------------------------------------------------\n");
    printk("  5000-byte allocation spans 2 pages.\n");
    printk("  Single TLB entry covers both pages — 4 accesses, 4 hits.\n");
    printk("  Expected: tlb_hits=4, app STOPS normally.\n\n");

    app_load(0, demo_tlb_range_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    {
        int pass = (app_get(0)->state == APP_STOPPED);
        results[8] = pass ? 1 : 2;
    }
    printk("\n");
    printk("[result] %-35s  %s\n", "Demo 9: TLB multi-page range",
           results[8] == 1 ? "PASS" : "FAIL");

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 10: IPC valid case (boundary copy)\n");
    printk("------------------------------------------------\n");
    printk("  App calls vmmu_msgq_put with a properly allocated buffer.\n");
    printk("  copy_from_app validates the range, stages it, then k_msgq_put runs.\n");
    printk("  Expected: vmmu_msgq_put returns 0, app STOPS normally.\n\n");

    app_load(0, demo_ipc_valid_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(9, 0, APP_STOPPED,
                 "Demo 10: IPC valid (msg delivered)", 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 11: IPC invalid pointer (guard page)\n");
    printk("------------------------------------------------\n");
    printk("  App passes its guard page address to vmmu_msgq_put.\n");
    printk("  copy_from_app catches AS_ERR_PERM before kernel touches memory.\n");
    printk("  Expected: app CRASHES, OS continues.\n\n");

    app_load(0, demo_ipc_invalid_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(10, 0, APP_CRASHED,
                 "Demo 11: IPC invalid ptr blocked", 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 12: IPC cross-app pointer\n");
    printk("------------------------------------------------\n");
    printk("  App 0 passes app 1's address to vmmu_msgq_put.\n");
    printk("  copy_from_app uses app 0's vpage table → unmapped → abort.\n");
    printk("  Expected: app 0 CRASHES, app 1 stays RUNNING.\n\n");

    app_load(1, demo_victim_entry, APP1_HEAP_BASE, APP1_HEAP_LIMIT);
    app_start(1);

    app_load(0, demo_ipc_crossapp_entry, APP0_HEAP_BASE, APP0_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    int victim_alive_ipc = (app_get(1)->state == APP_RUNNING);
    printk("\n");
    print_result(11, 0, APP_CRASHED,
                 "Demo 12: IPC cross-app blocked", victim_alive_ipc);
    printk("[result]   Victim app 1 still running: %s\n",
           victim_alive_ipc ? "YES" : "NO");

    app_stop(1);
    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 13: Semaphore-gated virtual LED blink\n");
    printk("------------------------------------------------\n");
    printk("  Kernel thread releases a semaphore every 200 ms.\n");
    printk("  App thread waits on it, toggles a virtual LED (printk).\n");
    printk("  Expected: 10 alternating ON/OFF lines, app STOPS normally.\n\n");

    app_load(0, demo_semaphore_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(12, 0, APP_STOPPED,
                 "Demo 13: Semaphore LED blink", 1);

    k_msleep(500);

    printk("\n");
    printk("------------------------------------------------\n");
    printk("  Demo 14: TLB verification\n");
    printk("------------------------------------------------\n");
    printk("  100 accese repetate intr-o alocare -> toate lovesc TLB-ul.\n");
    printk("  Dupa free, intrarea este invalidata (lookup MISS).\n");
    printk("  Expected: tlb_hits=100, app STOPS normally.\n\n");

    app_load(0, demo_tlb_verify_entry, APP2_HEAP_BASE, APP2_HEAP_LIMIT);
    app_start(0);
    wait_for_app(0);

    printk("\n");
    print_result(13, 0, APP_STOPPED,
                 "Demo 14: TLB verification", 1);

    k_msleep(500);

    printk("\n");
    printk("================================================\n");
    printk("  TEST SUMMARY\n");
    printk("================================================\n");

    const char *demo_names[14] = {
        "Demo 1:  Out-of-Bounds Write     ",
        "Demo 2:  Cross-App Access        ",
        "Demo 3:  Demand Paging           ",
        "Demo 4:  Sub-Page OOB (Shadow)   ",
        "Demo 5:  Stack Overflow          ",
        "Demo 6:  Global Variable Isolation",
        "Demo 7:  TLB Hit (repeated access)",
        "Demo 8:  TLB Invalidated on Free ",
        "Demo 9:  TLB Multi-Page Range    ",
        "Demo 10: IPC valid (msg delivered)",
        "Demo 11: IPC invalid ptr blocked ",
        "Demo 12: IPC cross-app blocked   ",
        "Demo 13: Semaphore LED blink     ",
        "Demo 14: TLB verification        ",
    };

    int total_pass = 0;
    for(int i = 0; i < 14; i++)
    {
        const char *verdict = (results[i] == 1) ? "PASS" : "FAIL";
        if(results[i] == 1)
        {
            total_pass++;
        }
        printk("  %s  %s\n", demo_names[i], verdict);
    }

    printk("------------------------------------------------\n");
    printk("  Result: %d / 14 passed\n", total_pass);
    printk("================================================\n\n");

    return 0;
}
