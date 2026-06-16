#include "tlb.h"

tlb_entry tlb_table[TLB_ENTRIES];

uint32_t tlb_hits = 0;

void tlb_flush_app(uint8_t app_id)
{
    for(uint32_t i = 0; i < TLB_ENTRIES; i++)
    {
        if(tlb_table[i].valid && tlb_table[i].app_id == app_id)
        {
            tlb_table[i].valid = false;
        }
    }
}
