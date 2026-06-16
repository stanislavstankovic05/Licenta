#ifndef HEAP_H
#define HEAP_H

#include "../virtual_space/vpage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HEAP_ALIGN_BYTES
#define HEAP_ALIGN_BYTES (16u)
#endif

#ifndef HEAP_MAX_ALLOCS
#define HEAP_MAX_ALLOCS (256u)
#endif

#ifndef HEAP_MAX_FREE_BLOCKS
#define HEAP_MAX_FREE_BLOCKS (256u)
#endif

typedef enum
{
    HEAP_OK = 0,
    HEAP_ERROR_OUT_OF_VIRTUAL_SPACE,
    HEAP_ERROR_OUT_OF_FRAMES,
    HEAP_ERROR_NO_FREE_BLOCK_AVAILABLE,
    HEAP_ERROR_ALLOCATION_TRACKING_FULL,
    HEAP_ERROR_FREE_LIST_FULL,
    HEAP_ERROR_INVALID_POINTER,
    HEAP_ERROR_DOUBLE_FREE,
    HEAP_ERROR_POINTER_NOT_ALLOCATION_BASE,
    HEAP_ERROR_CORRUPT_STATE,
} heap_status;

typedef struct heap_free_block
{
    vpointer start;
    size_t size;
    int32_t next;
} heap_free_block;

typedef enum
{
    HEAP_ALLOCATION_UNUSED = 0,
    HEAP_ALLOCATION_LIVE,
    HEAP_ALLOCATION_FREED,
} heap_allocation_state;

typedef struct heap_allocation_record
{
    vpointer base;
    size_t size;
    heap_allocation_state state;
    uint32_t tag;
} heap_allocation_record;

typedef struct heap
{
    vpointer heap_base;
    vpointer heap_mapped_end;
    vpointer heap_limit;

    heap_free_block free_blocks[HEAP_MAX_FREE_BLOCKS];
    int32_t free_head;
    int32_t free_unused_head;

    heap_allocation_record heap_allocation_record_array[HEAP_MAX_ALLOCS];

    size_t bytes_live;
    size_t bytes_mapped;
    bool demand_paging;
} heap;

typedef heap_status (*heap_mapper_function)(
    void *context,
    vpointer virtual_address_start,
    size_t number_of_pages);

heap_status heap_init(heap *app_heap, vpointer heap_base, vpointer heap_limit);

heap_status heap_malloc(heap *app_heap, size_t size, heap_mapper_function map_pages,
                        void *map_context, size_t page_size, vpointer *out_pointer);

heap_status heap_free(heap *app_heap, vpointer pointer);

heap_status heap_get_alloc_size(heap *app_heap, vpointer pointer, size_t *out_size);

heap_status heap_find_alloc(heap *app_heap, vpointer addr,
                            vpointer *out_base, size_t *out_size);

#endif
