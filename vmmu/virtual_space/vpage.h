#ifndef VPAGE_H
#define VPAGE_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define VPAGE_NULL 0u

#define PERM_READ 0b001
#define PERM_WRITE 0b010
#define PERM_EXECUTE 0b100

typedef struct vpage
{
    bool present;
    int32_t frame_id;
    uint8_t perms;
    uint32_t epoch;
} vpage;

typedef uintptr_t vpointer;

#endif
