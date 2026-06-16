#ifndef FRAME_H
#define FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_OWNER_KERNEL 0xFFFFu
#define FRAME_OWNER_NONE 0xFFFEu

typedef enum
{
    FRAME_FREE = 0,
    FRAME_USED = 1,
} frame_state;

typedef struct frame
{
    frame_state state;
    uint16_t owner;
    uint16_t flags;
    uint32_t generation;
    int32_t next_free;
} frame;

void frame_init(frame *frame_pointer, size_t frame_size, uintptr_t pool_start);
int32_t frame_alloc(uint16_t owner);
int frame_free(uint16_t owner, int32_t frame_id);
size_t frame_reclaim_owner(uint16_t owner);
size_t frame_count_owner(uint16_t owner);
uintptr_t frame_paddr(int32_t frame_id);

#endif
