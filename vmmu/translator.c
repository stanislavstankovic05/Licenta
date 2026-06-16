#include "translator.h"
#include "app/app.h"
#include "tlb.h"
#include "virtual_space/addrspace.h"
#include "virtual_space/vpage.h"
#include "vmmu.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define VMMU_STACK_GUARD_MARGIN 768u

#define VMMU_SRAM_LO ((uintptr_t)CONFIG_SRAM_BASE_ADDRESS)
#define VMMU_SRAM_HI (VMMU_SRAM_LO + (uintptr_t)CONFIG_SRAM_SIZE * 1024u)

#define VMMU_CODE_HI VMMU_SRAM_LO

extern uint32_t __vmmu_globals_size;
extern uint32_t __vmmu_peak_frames;

void *vmmu_globals_base(void)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return NULL;
    }
    return (void *)(uintptr_t)current_app->globals_frame;
}

void vmmu_stack_check(void)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return;
    }

    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));

    uintptr_t stack_bottom = k_current_get()->stack_info.start;

    if(sp < stack_bottom + VMMU_STACK_GUARD_MARGIN)
    {
        printk("[vmmu] STACK OVERFLOW app=%u sp=0x%08x bottom=0x%08x "
               "remaining=%d bytes\n",
               (unsigned)current_app->id,
               (unsigned)sp,
               (unsigned)stack_bottom,
               (int)(sp - stack_bottom));
        app_abort();
    }
}

struct search_stack_context
{
    uintptr_t lo;
    uintptr_t hi;
    app *target_app;
    const struct k_thread *self;
    bool found;
};

static void search_stack_callback(const struct k_thread *t, void *user_data)
{
    struct search_stack_context *context = (struct search_stack_context *)user_data;
    if(context->found)
    {
        return;
    }
    if(t == context->self)
    {
        return;
    }
    if((app *)t->custom_data != context->target_app)
    {
        return;
    }
    if(t->stack_info.size == 0)
    {
        return;
    }

    uintptr_t t_lo = t->stack_info.start;
    uintptr_t t_hi = t_lo + t->stack_info.size;
    if(context->lo >= t_lo && context->hi <= t_hi)
    {
        context->found = true;
    }
}

/* Validates a stack access [lo, hi): allowed only on the current thread's
 * own stack or a peer thread of the same app. Closes the "other apps'
 * stacks" bypass (see Annexe B). */
static bool strict_stack_ok(app *current_app, uintptr_t lo, uintptr_t hi)
{
    struct k_thread *me = k_current_get();

    if(me->stack_info.size != 0)
    {
        uintptr_t s_lo = me->stack_info.start;
        uintptr_t s_hi = s_lo + me->stack_info.size;
        if(lo >= s_lo && hi <= s_hi)
        {
            return true;
        }
    }
    else
    {
        if(lo >= VMMU_SRAM_LO && hi <= VMMU_SRAM_HI)
        {
            return true;
        }
    }

    struct search_stack_context context = {
        .lo = lo,
        .hi = hi,
        .target_app = current_app,
        .self = me,
        .found = false,
    };
    k_thread_foreach(search_stack_callback, &context);
    return context.found;
}

void vmmu_check(void *ptr, size_t len, uint8_t access)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return;
    }

    uintptr_t paddr;

#ifndef VMMU_DISABLE_TLB
    uint32_t vaddr32 = (uint32_t)(uintptr_t)ptr;
    if(tlb_lookup(vaddr32, (uint32_t)len, (uint8_t)current_app->id) == 0)
    {
        return;
    }
#endif

    int r = addrspace_translate(&current_app->address_space, (vpointer)ptr, len, access, &paddr);

    if(r == AS_OK)
    {
        if(vmmu_in_pool(paddr))
        {
            size_t remaining = len;
            vpointer cur_vaddr = (vpointer)(uintptr_t)ptr;
            uintptr_t cur_paddr = paddr;

            while(remaining > 0)
            {
                size_t page_offset = cur_paddr % PAGE_SIZE;
                size_t chunk = PAGE_SIZE - page_offset;
                if(chunk > remaining)
                    chunk = remaining;

                int sr = shadow_mem_check(cur_paddr, chunk, (uint8_t)current_app->id);
                if(sr == -1)
                {
                    printk("[vmmu] SHADOW FAULT app=%u ptr=%p len=%u "
                           "unallocated byte (sub-page OOB)\n",
                           (unsigned)current_app->id, ptr, (unsigned)len);
                    app_abort();
                }
                else if(sr == -2)
                {
                    printk("[vmmu] SHADOW FAULT app=%u ptr=%p len=%u "
                           "cross-app ownership violation\n",
                           (unsigned)current_app->id, ptr, (unsigned)len);
                    app_abort();
                }

                cur_vaddr += chunk;
                remaining -= chunk;

                if(remaining > 0)
                {
                    if(addrspace_translate(&current_app->address_space, (vpointer)cur_vaddr,
                                           remaining, access,
                                           &cur_paddr) != AS_OK)
                    {
                        break;
                    }
                }
            }
        }
        return;
    }

    if(r == AS_ERR_RANGE || r == AS_ERR_OVERFLOW)
    {
        uintptr_t lo = (uintptr_t)ptr;
        uintptr_t hi = lo + len;

        if(access == PERM_READ && hi <= VMMU_CODE_HI)
        {
            return;
        }

        bool allowed = false;
        if(strict_stack_ok(current_app, lo, hi))
        {
            allowed = true;
        }
        else if(lo >= VMMU_SRAM_LO && hi <= VMMU_SRAM_HI)
        {
            allowed = true;
        }

        if(allowed)
        {
#ifndef VMMU_DISABLE_TLB
            uint32_t page = (uint32_t)(lo & ~((uintptr_t)PAGE_SIZE - 1));
            tlb_insert(page, (uint32_t)PAGE_SIZE, (uint8_t)current_app->id);
#endif
            return;
        }

        {
            printk("[vmmu] FAULT app=%u ptr=%p len=%u out-of-range "
                   "access=%s\n",
                   (unsigned)current_app->id, ptr, (unsigned)len,
                   (access == PERM_WRITE) ? "WRITE" : "READ");
            app_abort();
        }
    }

    if(r == AS_ERR_UNMAPPED)
    {
        vpointer vaddr = (vpointer)(uintptr_t)ptr;
        if(vaddr >= current_app->heap_base && vaddr < current_app->heap_limit)
        {
            if(app_demand_alloc(current_app, vaddr) == 0)
            {
                return;
            }
        }
    }

    const char *reason;
    switch(r)
    {
    case AS_ERR_UNMAPPED:
        reason = "unmapped page";
        break;
    case AS_ERR_PERM:
        reason = "permission denied";
        break;
    case AS_ERR_EPOCH:
        reason = "stale pointer (UAF)";
        break;
    case AS_ERR_RANGE:
        reason = "address out of range";
        break;
    case AS_ERR_OVERFLOW:
        reason = "address overflow";
        break;
    default:
        reason = "unknown";
        break;
    }

    printk("[vmmu] FAULT app=%u ptr=%p len=%u %s access=%s\n",
           (unsigned)current_app->id,
           ptr,
           (unsigned)len,
           reason,
           (access == PERM_WRITE) ? "WRITE" : "READ");

    app_abort();
}

void *vmmu_malloc(size_t size)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return NULL;
    }

    vpointer out = 0;
    heap_status st = app_heap_malloc(current_app, size, &out);
    if(st != HEAP_OK)
    {
        printk("[vmmu] malloc failed: app=%u size=%u err=%d\n",
               (unsigned)current_app->id, (unsigned)size, (int)st);
        return NULL;
    }

    size_t marked = 0;
    while(marked < size)
    {
        vpointer cur = out + marked;
        size_t page_offset = cur % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - page_offset;
        if(chunk > size - marked)
            chunk = size - marked;

        uintptr_t paddr;
        if(addrspace_translate(&current_app->address_space, (vpointer)cur, chunk,
                               PERM_READ, &paddr) == AS_OK)
        {
            shadow_mem_mark(paddr, chunk, (uint8_t)current_app->id);
        }
        marked += chunk;
    }

    tlb_insert((uint32_t)out, (uint32_t)size, (uint8_t)current_app->id);

    return (void *)out;
}

void vmmu_free(void *ptr)
{
    app *current_app = app_current();
    if(!current_app || !ptr)
    {
        return;
    }

    vpointer vaddr = (vpointer)(uintptr_t)ptr;

    tlb_invalidate((uint32_t)vaddr, (uint8_t)current_app->id);

    size_t alloc_size = 0;
    app_heap_get_alloc_size(current_app, vaddr, &alloc_size);

    size_t cleared = 0;
    while(cleared < alloc_size || (cleared == 0 && alloc_size == 0))
    {
        vpointer cur = vaddr + cleared;
        uintptr_t paddr;
        if(addrspace_translate(&current_app->address_space, (vpointer)cur, 1,
                               PERM_READ, &paddr) == AS_OK)
        {
            size_t frame_base = paddr - (paddr % PAGE_SIZE);
            shadow_mem_clear(frame_base, PAGE_SIZE);
        }
        cleared += PAGE_SIZE;
        if(alloc_size == 0)
            break;
    }

    heap_status st = app_heap_free(current_app, vaddr);
    if(st != HEAP_OK)
    {
        printk("[vmmu] free failed: app=%u ptr=%p err=%d\n",
               (unsigned)current_app->id, ptr, (int)st);
    }
}
