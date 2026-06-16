#include "app.h"
#include "../frame/frame.h"
#include "../tlb.h"
#include "../translator.h"
#include "../vmmu.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

extern uint32_t __vmmu_globals_size;

K_THREAD_STACK_ARRAY_DEFINE(app_stacks, APP_MAX, APP_STACK_SIZE);

static app app_table[APP_MAX];

static heap_status app_map_pages(void *ctx, vpointer vstart, size_t npages)
{
    app *current_app = (app *)ctx;

    for(size_t i = 0; i < npages; i++)
    {
        int32_t fid = frame_alloc(current_app->id);
        if(fid < 0)
        {
            return HEAP_ERROR_OUT_OF_FRAMES;
        }

        uint32_t vp = (uint32_t)((vstart + i * PAGE_SIZE) / PAGE_SIZE);
        int r = addrspace_map(&current_app->address_space, vp, fid, PERM_READ | PERM_WRITE);
        if(r != AS_OK)
        {
            frame_free(current_app->id, fid);
            return HEAP_ERROR_OUT_OF_FRAMES;
        }
    }

    return HEAP_OK;
}

static void app_entry_wrapper(void *p1, void *p2, void *p3)
{
    app *current_app = (app *)p1;
    k_thread_custom_data_set(current_app);

    if(__vmmu_globals_size > 0)
    {
        void *gframe = vmmu_malloc(__vmmu_globals_size);
        if(gframe)
        {
            current_app->globals_frame = (vpointer)(uintptr_t)gframe;
            memset(gframe, 0, __vmmu_globals_size);
            printk("[vmmu] app %u globals frame: vaddr=0x%08x size=%u\n",
                   (unsigned)current_app->id,
                   (unsigned)current_app->globals_frame,
                   (unsigned)__vmmu_globals_size);
        }
        else
        {
            printk("[vmmu] app %u: globals frame alloc failed\n",
                   (unsigned)current_app->id);
        }
    }

    current_app->entry(p1, p2, p3);
    current_app->state = APP_STOPPED;
}

void app_subsystem_init(void)
{
    for(app_id i = 0; i < APP_MAX; i++)
    {
        app_table[i].id = i;
        app_table[i].state = APP_UNUSED;
        app_table[i].stack = app_stacks[i];
        app_table[i].globals_frame = 0;
    }
}

int app_load(app_id id, k_thread_entry_t entry,
             vpointer heap_base, vpointer heap_limit)
{
    if(id >= APP_MAX)
    {
        return APP_ERR_INVALID_ID;
    }

    app *current_app = &app_table[id];
    if(current_app->state == APP_CRASHED || current_app->state == APP_STOPPED)
    {
        app_cleanup(id);
    }

    if(current_app->state != APP_UNUSED)
    {
        return APP_ERR_WRONG_STATE;
    }

    if(heap_base < PAGE_SIZE || heap_base >= heap_limit)
    {
        return APP_ERR_LOAD_FAILED;
    }

    current_app->entry = entry;
    current_app->heap_base = heap_base;
    current_app->heap_limit = heap_limit;

    addrspace_init(&current_app->address_space);
    heap_init(&current_app->heap, heap_base, heap_limit);

    uint32_t guard_vp = (uint32_t)(heap_limit / PAGE_SIZE);
    if(guard_vp < MAX_VPAGES)
    {
        addrspace_map_guard(&current_app->address_space, guard_vp);
    }

    current_app->max_frames = (uint32_t)((heap_limit - heap_base) / PAGE_SIZE);

    current_app->state = APP_INIT;
    return APP_OK;
}

int app_load_bounded(app_id id, k_thread_entry_t entry,
                     vpointer heap_base, vpointer heap_limit,
                     uint32_t peak_frames)
{
    uint32_t budget = (uint32_t)((heap_limit - heap_base) / PAGE_SIZE);

    if(peak_frames != UINT32_MAX && peak_frames > budget)
    {
        printk("[vmmu] app %u rejected: peak_frames=%u > budget=%u\n",
               (unsigned)id, (unsigned)peak_frames, (unsigned)budget);
        return APP_ERR_LOAD_FAILED;
    }

    int r = app_load(id, entry, heap_base, heap_limit);
    if(r != APP_OK)
    {
        return r;
    }

    app *current_app = &app_table[id];

    current_app->max_frames = (peak_frames < budget) ? peak_frames : budget;

    current_app->heap.demand_paging = true;

    return APP_OK;
}

int app_demand_alloc(app *current_app, vpointer vaddr)
{
    if(!current_app)
    {
        return -1;
    }

    if(frame_count_owner(current_app->id) >= current_app->max_frames)
    {
        printk("[vmmu] demand fault: frame budget exhausted app=%u max=%u\n",
               (unsigned)current_app->id, (unsigned)current_app->max_frames);
        return -1;
    }

    uint32_t vp = (uint32_t)(vaddr / PAGE_SIZE);
    int32_t fid = frame_alloc(current_app->id);

    if(fid < 0)
    {
        printk("[vmmu] demand fault: no free frames app=%u vp=%u\n",
               (unsigned)current_app->id, (unsigned)vp);
        return -1;
    }

    int r = addrspace_map(&current_app->address_space, vp, fid, PERM_READ | PERM_WRITE);
    if(r != AS_OK)
    {
        frame_free(current_app->id, fid);
        printk("[vmmu] demand fault: addrspace_map failed app=%u vp=%u err=%d\n",
               (unsigned)current_app->id, (unsigned)vp, r);
        return -1;
    }

    return 0;
}

int app_start(app_id id)
{
    if(id >= APP_MAX)
    {
        return APP_ERR_INVALID_ID;
    }

    app *current_app = &app_table[id];

    if(current_app->state != APP_INIT)
    {
        return APP_ERR_WRONG_STATE;
    }
    current_app->state = APP_RUNNING;

    k_thread_create(&current_app->thread,
                    current_app->stack,
                    APP_STACK_SIZE,
                    app_entry_wrapper,
                    current_app, NULL, NULL,
                    APP_PRIORITY, 0, K_NO_WAIT);

    return APP_OK;
}

void app_stop(app_id id)
{
    if(id >= APP_MAX)
    {
        return;
    }

    app *current_app = &app_table[id];

    if(current_app->state != APP_RUNNING)
    {
        return;
    }

    k_thread_abort(&current_app->thread);
    current_app->state = APP_STOPPED;
}

void app_abort(void)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return;
    }
    current_app->state = APP_CRASHED;
    printk("[vmmu] app %u crashed\n", (unsigned)current_app->id);
    k_thread_abort(k_current_get());
}

void app_cleanup(app_id id)
{
    if(id >= APP_MAX)
    {
        return;
    }

    app *current_app = &app_table[id];
    tlb_flush_app((uint8_t)current_app->id);

    shadow_mem_reclaim_owner(current_app->id);
    frame_reclaim_owner(current_app->id);

    addrspace_bump_epoch(&current_app->address_space);
    addrspace_init(&current_app->address_space);
    current_app->globals_frame = 0;

    current_app->state = APP_UNUSED;
}

int app_restart(app_id id, k_thread_entry_t entry,
                vpointer heap_base, vpointer heap_limit)
{
    int r = app_load(id, entry, heap_base, heap_limit);
    if(r != APP_OK)
    {
        return r;
    }

    return app_start(id);
}

app *app_current(void)
{
    return (app *)k_thread_custom_data_get();
}

app *app_get(app_id id)
{
    if(id >= APP_MAX)
    {
        return NULL;
    }

    return &app_table[id];
}

heap_status app_heap_malloc(app *current_app, size_t size, vpointer *out)
{
    if(!current_app || !out)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }
    return heap_malloc(&current_app->heap, size, app_map_pages, current_app, PAGE_SIZE, out);
}

heap_status app_heap_free(app *current_app, vpointer ptr)
{
    if(!current_app)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }
    return heap_free(&current_app->heap, ptr);
}

heap_status app_heap_get_alloc_size(app *current_app, vpointer ptr,
                                    size_t *out_size)
{
    if(!current_app || !out_size)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }
    return heap_get_alloc_size(&current_app->heap, ptr, out_size);
}

heap_status app_heap_find_alloc(app *current_app, vpointer addr,
                                vpointer *out_base, size_t *out_size)
{
    if(!current_app || !out_base || !out_size)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }
    return heap_find_alloc(&current_app->heap, addr, out_base, out_size);
}
