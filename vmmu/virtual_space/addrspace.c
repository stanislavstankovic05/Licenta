#include "addrspace.h"
#include "../frame/frame.h"

void addrspace_init(addrspace *as)
{
    if(!as)
    {
        return;
    }

    as->epoch = 0;

    for(uint32_t i = 0; i < MAX_VPAGES; i++)
    {
        as->pages[i].present = false;
        as->pages[i].frame_id = -1;
        as->pages[i].perms = 0;
        as->pages[i].epoch = as->epoch;
    }
}

void addrspace_bump_epoch(addrspace *as)
{
    if(!as)
    {
        return;
    }
    as->epoch++;
}

int addrspace_map(addrspace *as, uint32_t vp, int32_t frame_id, uint8_t perms)
{
    if(!as)
    {
        return AS_ERR_BADARG;
    }
    if(vp >= MAX_VPAGES)
    {
        return AS_ERR_RANGE;
    }
    if(vp == VPAGE_NULL)
    {
        return AS_ERR_RANGE;
    }
    if(frame_id < 0)
    {
        return AS_ERR_BADARG;
    }

    vpage *p = &as->pages[vp];
    if(p->present)
    {
        return AS_ERR_ALREADY;
    }

    p->present = true;
    p->frame_id = frame_id;
    p->perms = perms;
    p->epoch = as->epoch;

    return AS_OK;
}

int addrspace_map_guard(addrspace *as, uint32_t vp)
{
    if(!as)
    {
        return AS_ERR_BADARG;
    }
    if(vp >= MAX_VPAGES)
    {
        return AS_ERR_RANGE;
    }
    if(vp == VPAGE_NULL)
    {
        return AS_ERR_RANGE;
    }

    vpage *p = &as->pages[vp];
    if(p->present)
    {
        return AS_ERR_ALREADY;
    }

    p->present = true;
    p->frame_id = -1;
    p->perms = 0;
    p->epoch = as->epoch;

    return AS_OK;
}

int addrspace_unmap(addrspace *as, uint32_t vp)
{
    if(!as)
    {
        return AS_ERR_BADARG;
    }
    if(vp >= MAX_VPAGES)
    {
        return AS_ERR_RANGE;
    }
    if(vp == VPAGE_NULL)
    {
        return AS_ERR_RANGE;
    }

    vpage *p = &as->pages[vp];
    if(!p->present)
    {
        return AS_ERR_UNMAPPED;
    }

    int32_t frame_id = p->frame_id;

    p->present = false;
    p->frame_id = -1;
    p->perms = 0;
    p->epoch = as->epoch;

    return frame_id;
}

int addrspace_translate(addrspace *as, vpointer ptr, size_t len,
                        uint8_t access, uintptr_t *out_paddr)
{
    if(!as || !out_paddr)
    {
        return AS_ERR_BADARG;
    }
    if(len == 0)
    {
        return AS_ERR_BADARG;
    }

    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + len - 1;

    if(end < start)
    {
        return AS_ERR_OVERFLOW;
    }

    uint32_t start_page = (uint32_t)(start / PAGE_SIZE);
    uint32_t end_page = (uint32_t)(end / PAGE_SIZE);

    if(end_page >= MAX_VPAGES)
    {
        return AS_ERR_RANGE;
    }

    if(start_page == VPAGE_NULL)
    {
        return AS_ERR_UNMAPPED;
    }

    for(uint32_t i = start_page; i <= end_page; i++)
    {
        if(i == VPAGE_NULL)
        {
            return AS_ERR_UNMAPPED;
        }

        vpage *p = &as->pages[i];

        if(!p->present)
        {
            return AS_ERR_UNMAPPED;
        }
        if((p->perms & access) != access)
        {
            return AS_ERR_PERM;
        }
        if(p->epoch != as->epoch)
        {
            return AS_ERR_EPOCH;
        }
    }

    uint32_t offset = (uint32_t)(start % PAGE_SIZE);
    vpage *p0 = &as->pages[start_page];

    uintptr_t base = frame_paddr(p0->frame_id);
    if(base == 0)
    {
        return AS_ERR_BADARG;
    }

    *out_paddr = base + offset;
    return AS_OK;
}
