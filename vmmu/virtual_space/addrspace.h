#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "vpage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_VPAGES 1024

typedef enum
{
    AS_OK = 0,
    AS_ERR_RANGE = -1,
    AS_ERR_ALREADY = -2,
    AS_ERR_UNMAPPED = -3,
    AS_ERR_PERM = -4,
    AS_ERR_EPOCH = -5,
    AS_ERR_OVERFLOW = -6,
    AS_ERR_BADARG = -7
} addrspace_error;

typedef struct addrspace
{
    vpage pages[MAX_VPAGES];
    uint32_t epoch;
} addrspace;

void addrspace_init(addrspace *as);
void addrspace_bump_epoch(addrspace *as);
int addrspace_map(addrspace *as, uint32_t vp, int32_t frame_id, uint8_t perms);
int addrspace_map_guard(addrspace *as, uint32_t vp);
int addrspace_unmap(addrspace *as, uint32_t vp);
int addrspace_translate(addrspace *as, vpointer ptr, size_t len,
                        uint8_t access, uintptr_t *out_paddr);

#endif
