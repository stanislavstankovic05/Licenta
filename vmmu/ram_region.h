#ifndef RAM_REGION_H
#define RAM_REGION_H

#include <stdbool.h>
#include <stdint.h>

#ifndef RAM_BASE_ADDR
#define RAM_BASE_ADDR (0x20000000UL)
#endif

#ifndef RAM_SIZE_BYTES
#define RAM_SIZE_BYTES (1UL * 1024UL * 1024UL)
#endif

#define RAM_END_ADDR (RAM_BASE_ADDR + (uintptr_t)RAM_SIZE_BYTES)

#ifndef FRAME_SIZE_BYTES
#define FRAME_SIZE_BYTES (4096UL)
#endif

#ifdef __ZEPHYR__
extern uint8_t __bss_end[];
#define __bss_end__ __bss_end
#else
extern uint8_t __bss_end__[];
#endif

#ifndef EXTRA_RESERVED_BYTES
#define EXTRA_RESERVED_BYTES (0UL)
#endif

static inline uintptr_t rr_align_up(uintptr_t v, uintptr_t a)
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

static inline uintptr_t rr_align_down(uintptr_t v, uintptr_t a)
{
    uintptr_t rem = v % a;
    return v - rem;
}

static inline uintptr_t usable_ram_start(void)
{
    uintptr_t kernel_end = (uintptr_t)__bss_end__;
    uintptr_t start = kernel_end + (uintptr_t)EXTRA_RESERVED_BYTES;
    return rr_align_up(start, (uintptr_t)FRAME_SIZE_BYTES);
}

static inline uintptr_t usable_ram_end(void)
{
    return rr_align_down((uintptr_t)RAM_END_ADDR, (uintptr_t)FRAME_SIZE_BYTES);
}

typedef enum
{
    RAM_REGION_OK = 0,
    RAM_REGION_ERR_ORDER,
    RAM_REGION_ERR_ALIGN,
    RAM_REGION_ERR_OUT_OF_BOUNDS,
    RAM_REGION_ERR_TOO_SMALL,
} ram_region_status_t;

static inline ram_region_status_t ram_region_sanity_check(void)
{
    uintptr_t start = usable_ram_start();
    uintptr_t end = usable_ram_end();

    if(start >= end)
    {
        return RAM_REGION_ERR_ORDER;
    }

    if((start % (uintptr_t)FRAME_SIZE_BYTES) != 0u ||
       (end % (uintptr_t)FRAME_SIZE_BYTES) != 0u)
    {
        return RAM_REGION_ERR_ALIGN;
    }

    if(start < (uintptr_t)RAM_BASE_ADDR || end > (uintptr_t)RAM_END_ADDR)
    {
        return RAM_REGION_ERR_OUT_OF_BOUNDS;
    }

    if((end - start) < (uintptr_t)FRAME_SIZE_BYTES)
    {
        return RAM_REGION_ERR_TOO_SMALL;
    }

    return RAM_REGION_OK;
}

#endif
