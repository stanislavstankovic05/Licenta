#include "frame.h"
#include "../virtual_space/vpage.h"
#include <stdio.h>

static frame *frame_list;
static size_t frame_pool_count;
static uintptr_t frame_pool_start;
static int32_t frame_free_head;
static size_t frame_free_count;

uintptr_t frame_paddr(int32_t frame_id)
{
    if(frame_id < 0 || (size_t)frame_id >= frame_pool_count)
    {
        return (uintptr_t)0;
    }
    return frame_pool_start + ((uintptr_t)frame_id * PAGE_SIZE);
}

void frame_init(frame *frame_pointer, size_t frame_size, uintptr_t pool_start)
{
    frame_list = frame_pointer;
    frame_pool_count = frame_size;
    frame_pool_start = pool_start;

    if(frame_pool_count == 0)
    {
        frame_free_head = -1;
        printf("not enough space, frame_pool_count is 0\n");
        return;
    }

    for(int32_t index = 0; index < (int32_t)frame_pool_count - 1; ++index)
    {
        frame_list[index].state = FRAME_FREE;
        frame_list[index].owner = FRAME_OWNER_NONE;
        frame_list[index].next_free = index + 1;
    }

    frame_list[frame_pool_count - 1].state = FRAME_FREE;
    frame_list[frame_pool_count - 1].owner = FRAME_OWNER_NONE;
    frame_list[frame_pool_count - 1].next_free = -1;

    frame_free_head = 0;
    frame_free_count = frame_pool_count;
}

int32_t frame_alloc(uint16_t owner)
{
    if(owner == FRAME_OWNER_NONE)
    {
        return -1;
    }
    if(frame_free_head < 0)
    {
        return -1;
    }

    int32_t index = frame_free_head;

    if((size_t)index >= frame_pool_count)
    {
        return -1;
    }

    if(frame_list[index].state != FRAME_FREE ||
       frame_list[index].owner != FRAME_OWNER_NONE)
    {
        return -1;
    }

    frame_free_head = frame_list[index].next_free;
    frame_list[index].owner = owner;
    frame_list[index].state = FRAME_USED;

    if(frame_free_count > 0)
    {
        frame_free_count--;
    }

    return index;
}

static int frame_free_unchecked(int32_t frame_id)
{
    frame_list[frame_id].state = FRAME_FREE;
    frame_list[frame_id].owner = FRAME_OWNER_NONE;
    frame_list[frame_id].next_free = frame_free_head;

    frame_free_head = frame_id;
    frame_free_count++;

    return 0;
}

int frame_free(uint16_t owner, int32_t frame_id)
{
    if(frame_id < 0 || (size_t)frame_id >= frame_pool_count)
    {
        return -1;
    }
    if(frame_list[frame_id].state != FRAME_USED)
    {
        return -1;
    }
    if(frame_list[frame_id].owner != owner && owner != FRAME_OWNER_KERNEL)
    {
        return -1;
    }

    return frame_free_unchecked(frame_id);
}

size_t frame_count_owner(uint16_t owner)
{
    if(owner == FRAME_OWNER_NONE)
    {
        return 0;
    }

    size_t count = 0;
    for(size_t i = 0; i < frame_pool_count; ++i)
    {
        if(frame_list[i].state == FRAME_USED && frame_list[i].owner == owner)
        {
            count++;
        }
    }

    return count;
}

size_t frame_reclaim_owner(uint16_t owner)
{
    if(owner == FRAME_OWNER_NONE)
    {
        return 0;
    }

    size_t reclaimed = 0;
    for(size_t i = 0; i < frame_pool_count; ++i)
    {
        if(frame_list[i].state == FRAME_USED && frame_list[i].owner == owner)
        {
            frame_free_unchecked((int32_t)i);
            reclaimed++;
        }
    }

    return reclaimed;
}
