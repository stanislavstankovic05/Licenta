#include "heap.h"
#include <stddef.h>

static int32_t index_stack_pop(int32_t *head, heap_free_block *blocks)
{
    if(*head < 0)
    {
        return -1;
    }
    int32_t idx = *head;
    *head = blocks[idx].next;
    return idx;
}

static void index_stack_push(int32_t *head, heap_free_block *blocks, int32_t idx)
{
    blocks[idx].next = *head;
    *head = idx;
}

static void heap_coalesce(heap *app_heap)
{
    bool merged;
    do
    {
        merged = false;
        for(int32_t a = app_heap->free_head; a >= 0; a = app_heap->free_blocks[a].next)
        {
            for(int32_t b = app_heap->free_head; b >= 0; b = app_heap->free_blocks[b].next)
            {
                if(a == b)
                {
                    continue;
                }
                if(app_heap->free_blocks[a].start + app_heap->free_blocks[a].size !=
                   app_heap->free_blocks[b].start)
                {
                    continue;
                }

                app_heap->free_blocks[a].size += app_heap->free_blocks[b].size;

                if(app_heap->free_head == b)
                {
                    app_heap->free_head = app_heap->free_blocks[b].next;
                }
                else
                {
                    for(int32_t p = app_heap->free_head; p >= 0; p = app_heap->free_blocks[p].next)
                    {
                        if(app_heap->free_blocks[p].next == b)
                        {
                            app_heap->free_blocks[p].next = app_heap->free_blocks[b].next;
                            break;
                        }
                    }
                }

                index_stack_push(&app_heap->free_unused_head, app_heap->free_blocks, b);
                merged = true;
                break;
            }
            if(merged)
            {
                break;
            }
        }
    } while(merged);
}

heap_status heap_init(heap *app_heap, vpointer heap_base, vpointer heap_limit)
{
    if(!app_heap || heap_base >= heap_limit)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }

    app_heap->heap_base = heap_base;
    app_heap->heap_mapped_end = heap_base;
    app_heap->heap_limit = heap_limit;
    app_heap->free_head = -1;
    app_heap->bytes_live = 0;
    app_heap->bytes_mapped = 0;
    app_heap->demand_paging = false;

    for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
    {
        app_heap->heap_allocation_record_array[i].state = HEAP_ALLOCATION_UNUSED;
    }

    for(int32_t i = 0; i < (int32_t)HEAP_MAX_FREE_BLOCKS - 1; i++)
    {
        app_heap->free_blocks[i].next = i + 1;
    }
    app_heap->free_blocks[HEAP_MAX_FREE_BLOCKS - 1].next = -1;
    app_heap->free_unused_head = 0;

    return HEAP_OK;
}

heap_status heap_malloc(heap *app_heap, size_t size, heap_mapper_function map_pages,
                        void *map_context, size_t page_size, vpointer *out_pointer)
{
    if(!app_heap || !out_pointer || size == 0)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }

    size_t rem = size % HEAP_ALIGN_BYTES;
    if(rem != 0)
    {
        size += HEAP_ALIGN_BYTES - rem;
    }

    int32_t alloc_slot = -1;
    for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
    {
        if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_UNUSED)
        {
            alloc_slot = i;
            break;
        }
    }
    if(alloc_slot < 0)
    {
        for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
        {
            if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_FREED)
            {
                alloc_slot = i;
                break;
            }
        }
    }
    if(alloc_slot < 0)
    {
        return HEAP_ERROR_ALLOCATION_TRACKING_FULL;
    }

    int32_t best = -1;
    int32_t best_prev = -1;
    size_t best_size = (size_t)-1;
    int32_t prev = -1;

    for(int32_t cur = app_heap->free_head; cur >= 0; cur = app_heap->free_blocks[cur].next)
    {
        size_t block_size = app_heap->free_blocks[cur].size;
        if(block_size >= size && block_size < best_size)
        {
            best = cur;
            best_prev = prev;
            best_size = block_size;
        }
        prev = cur;
    }

    if(best < 0)
    {
        if(!app_heap->demand_paging && !map_pages)
        {
            return HEAP_ERROR_OUT_OF_FRAMES;
        }

        size_t pages = (size + page_size - 1) / page_size;
        vpointer grow_end = app_heap->heap_mapped_end + pages * page_size;

        if(grow_end > app_heap->heap_limit)
        {
            return HEAP_ERROR_OUT_OF_VIRTUAL_SPACE;
        }

        if(!app_heap->demand_paging)
        {
            heap_status st = map_pages(map_context, app_heap->heap_mapped_end, pages);
            if(st != HEAP_OK)
            {
                return HEAP_ERROR_OUT_OF_FRAMES;
            }
        }

        int32_t slot = index_stack_pop(&app_heap->free_unused_head, app_heap->free_blocks);
        if(slot < 0)
        {
            return HEAP_ERROR_FREE_LIST_FULL;
        }

        app_heap->free_blocks[slot].start = app_heap->heap_mapped_end;
        app_heap->free_blocks[slot].size = pages * page_size;
        app_heap->free_blocks[slot].next = app_heap->free_head;
        app_heap->free_head = slot;

        app_heap->bytes_mapped += pages * page_size;
        app_heap->heap_mapped_end = grow_end;

        best = slot;
        best_prev = -1;
        best_size = app_heap->free_blocks[slot].size;
    }

    vpointer alloc_start = app_heap->free_blocks[best].start;
    size_t remainder = best_size - size;

    if(remainder >= HEAP_ALIGN_BYTES)
    {
        app_heap->free_blocks[best].start += size;
        app_heap->free_blocks[best].size = remainder;
    }
    else
    {
        if(best_prev < 0)
        {
            app_heap->free_head = app_heap->free_blocks[best].next;
        }
        else
        {
            app_heap->free_blocks[best_prev].next = app_heap->free_blocks[best].next;
        }
        index_stack_push(&app_heap->free_unused_head, app_heap->free_blocks, best);
    }

    app_heap->heap_allocation_record_array[alloc_slot].base = alloc_start;
    app_heap->heap_allocation_record_array[alloc_slot].size = size;
    app_heap->heap_allocation_record_array[alloc_slot].state = HEAP_ALLOCATION_LIVE;
    app_heap->bytes_live += size;

    *out_pointer = alloc_start;
    return HEAP_OK;
}

heap_status heap_get_alloc_size(heap *app_heap, vpointer pointer,
                                size_t *out_size)
{
    if(!app_heap || !out_size)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }

    for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
    {
        if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_LIVE &&
           app_heap->heap_allocation_record_array[i].base == pointer)
        {
            *out_size = app_heap->heap_allocation_record_array[i].size;
            return HEAP_OK;
        }
    }
    return HEAP_ERROR_INVALID_POINTER;
}

heap_status heap_find_alloc(heap *app_heap, vpointer addr,
                            vpointer *out_base, size_t *out_size)
{
    if(!app_heap || !out_base || !out_size)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }

    for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
    {
        if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_LIVE)
        {
            vpointer base = app_heap->heap_allocation_record_array[i].base;
            size_t size = app_heap->heap_allocation_record_array[i].size;
            if(addr >= base && addr < base + size)
            {
                *out_base = base;
                *out_size = size;
                return HEAP_OK;
            }
        }
    }
    return HEAP_ERROR_INVALID_POINTER;
}

heap_status heap_free(heap *app_heap, vpointer pointer)
{
    if(!app_heap)
    {
        return HEAP_ERROR_CORRUPT_STATE;
    }

    int32_t slot = -1;
    for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
    {
        if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_LIVE &&
           app_heap->heap_allocation_record_array[i].base == pointer)
        {
            slot = i;
            break;
        }
    }

    if(slot < 0)
    {
        for(int32_t i = 0; i < (int32_t)HEAP_MAX_ALLOCS; i++)
        {
            if(app_heap->heap_allocation_record_array[i].state == HEAP_ALLOCATION_FREED &&
               app_heap->heap_allocation_record_array[i].base == pointer)
            {
                return HEAP_ERROR_DOUBLE_FREE;
            }
        }
        return HEAP_ERROR_INVALID_POINTER;
    }

    size_t sz = app_heap->heap_allocation_record_array[slot].size;
    app_heap->heap_allocation_record_array[slot].state = HEAP_ALLOCATION_FREED;
    app_heap->bytes_live -= sz;

    int32_t fb = index_stack_pop(&app_heap->free_unused_head, app_heap->free_blocks);
    if(fb < 0)
    {
        return HEAP_ERROR_FREE_LIST_FULL;
    }

    app_heap->free_blocks[fb].start = pointer;
    app_heap->free_blocks[fb].size = sz;
    app_heap->free_blocks[fb].next = app_heap->free_head;
    app_heap->free_head = fb;

    heap_coalesce(app_heap);

    return HEAP_OK;
}
