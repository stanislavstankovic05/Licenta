#include "bump.h"

static uintptr_t current_bump = 0;
static uintptr_t bump_limit = 0;

static inline uintptr_t bump_align_up(uintptr_t v, uintptr_t a)
{
    uintptr_t rem = v % a;
    if(rem == 0)
    {
        return v;
    }
    else
    {
        return v + (a - rem);
    }
}

static inline uintptr_t bump_align_down(uintptr_t v, uintptr_t a)
{
    uintptr_t rem = v % a;
    return v - rem;
}

void bump_init(uintptr_t base, uintptr_t limit)
{
    current_bump = base;
    bump_limit = limit;
}

void *bump_alloc(size_t size, size_t align)
{
    if(align == 0)
    {
        align = 1;
    }

    if((align & (align - 1)) != 0)
    {
        return NULL;
    }

    uintptr_t cur_aligned = bump_align_up(current_bump, (uintptr_t)align);
    uintptr_t next = cur_aligned + (uintptr_t)size;

    if(next < cur_aligned || next > bump_limit)
    {
        return NULL;
    }

    current_bump = next;
    return (void *)cur_aligned;
}

uintptr_t bump_get_current(void)
{
    return current_bump;
}

uintptr_t bump_get_limit(void)
{
    return bump_limit;
}
