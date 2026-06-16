#ifndef TLB_H
#define TLB_H

#include "virtual_space/vpage.h"
#include <stdbool.h>
#include <stdint.h>

#define TLB_ENTRIES 32u
#define PAGE_SHIFT 12u
#define TLB_INVALID 0xFFFFFFFFu

typedef struct
{
    uint32_t base;
    uint32_t limit;
    uint8_t app_id;
    bool valid;
} tlb_entry;

extern tlb_entry tlb_table[TLB_ENTRIES];
extern uint32_t tlb_hits;

static inline uint32_t tlb_index(uint32_t base)
{
    return (base >> PAGE_SHIFT) & (TLB_ENTRIES - 1u);
}

static inline int tlb_lookup(uint32_t vaddr, uint32_t len, uint8_t app_id)
{
    uint32_t idx = tlb_index(vaddr);
    tlb_entry *e = &tlb_table[idx];

    if(e->valid &&
       e->app_id == app_id &&
       vaddr >= e->base &&
       vaddr + len <= e->limit)
    {
        tlb_hits++;
        return 0;
    }
    return -1;
}

static inline void tlb_insert(uint32_t base, uint32_t size, uint8_t app_id)
{
    tlb_entry *e = &tlb_table[tlb_index(base)];
    e->base = base;
    e->limit = base + size;
    e->app_id = app_id;
    e->valid = true;
}

static inline void tlb_invalidate(uint32_t base, uint8_t app_id)
{
    tlb_entry *e = &tlb_table[tlb_index(base)];
    if(e->valid && e->base == base && e->app_id == app_id)
    {
        e->valid = false;
    }
}

void tlb_flush_app(uint8_t app_id);

static inline void tlb_reset_stats(void)
{
    tlb_hits = 0;
}

#endif
