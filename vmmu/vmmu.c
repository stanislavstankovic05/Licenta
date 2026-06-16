#include "vmmu.h"
#include "frame/frame.h"
#include "virtual_space/vpage.h"
#include <string.h>
#include <zephyr/sys/printk.h>

#ifndef VMMU_NUM_FRAMES
#define VMMU_NUM_FRAMES 64
#endif

static frame vmmu_frame_table[VMMU_NUM_FRAMES];
static uint8_t vmmu_frame_pool[VMMU_NUM_FRAMES * PAGE_SIZE];

#define SHADOW_CHUNK 16u

static uint8_t shadow_mem[(VMMU_NUM_FRAMES * PAGE_SIZE) / SHADOW_CHUNK];

void vmmu_init(void)
{
    frame_init(vmmu_frame_table, VMMU_NUM_FRAMES,
               (uintptr_t)vmmu_frame_pool);

    memset(shadow_mem, 0, sizeof(shadow_mem));

    printk("VMMU init: frames=%u pool=[%p..%p) shadow=%p\n",
           (unsigned)VMMU_NUM_FRAMES,
           (void *)vmmu_frame_pool,
           (void *)(vmmu_frame_pool + sizeof(vmmu_frame_pool)),
           (void *)shadow_mem);
}

bool vmmu_in_pool(uintptr_t paddr)
{
    uintptr_t base = (uintptr_t)vmmu_frame_pool;
    return paddr >= base && paddr < base + sizeof(vmmu_frame_pool);
}

void shadow_mem_mark(uintptr_t paddr, size_t len, uint8_t app_id)
{
    uintptr_t base = (uintptr_t)vmmu_frame_pool;
    if(paddr < base || paddr + len > base + sizeof(vmmu_frame_pool))
    {
        return;
    }
    (void)app_id;
    size_t off = paddr - base;
    size_t chunk0 = off / SHADOW_CHUNK;
    size_t nfull = len / SHADOW_CHUNK;
    size_t r = len % SHADOW_CHUNK;

    for(size_t i = 0; i < nfull; i++)
    {
        shadow_mem[chunk0 + i] = 16u;
    }
    if(r != 0u)
    {
        shadow_mem[chunk0 + nfull] = (uint8_t)r;
    }
}

void shadow_mem_clear(uintptr_t paddr, size_t len)
{
    uintptr_t base = (uintptr_t)vmmu_frame_pool;
    if(paddr < base || paddr + len > base + sizeof(vmmu_frame_pool))
    {
        return;
    }
    size_t off = paddr - base;
    size_t chunk0 = off / SHADOW_CHUNK;
    size_t nchunks = (len + SHADOW_CHUNK - 1u) / SHADOW_CHUNK;

    for(size_t i = 0; i < nchunks; i++)
    {
        shadow_mem[chunk0 + i] = 0u;
    }
}

void shadow_mem_reclaim_owner(uint8_t app_id)
{
    size_t chunks_per_frame = PAGE_SIZE / SHADOW_CHUNK;
    for(size_t f = 0; f < VMMU_NUM_FRAMES; f++)
    {
        if(vmmu_frame_table[f].state == FRAME_USED &&
           vmmu_frame_table[f].owner == app_id)
        {
            size_t chunk0 = f * chunks_per_frame;
            for(size_t i = 0; i < chunks_per_frame; i++)
            {
                shadow_mem[chunk0 + i] = 0u;
            }
        }
    }
}

int shadow_mem_check(uintptr_t paddr, size_t len, uint8_t app_id)
{
    uintptr_t base = (uintptr_t)vmmu_frame_pool;
    if(paddr < base || paddr + len > base + sizeof(vmmu_frame_pool))
    {
        return 0;
    }

    (void)app_id;
    size_t off = paddr - base;
    for(size_t i = 0; i < len; i++)
    {
        size_t o = off + i;
        uint8_t cnt = shadow_mem[o / SHADOW_CHUNK];

        if(cnt == 0u)
        {
            return -1;
        }
        if((o % SHADOW_CHUNK) >= cnt)
        {
            return -1;
        }
    }
    return 0;
}
