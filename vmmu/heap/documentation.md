# heap — Per-App Virtual Heap Allocator

A best-fit heap allocator that manages a virtual address range for one app.
It does not allocate physical frames directly — it calls a `map_pages` callback
when it needs to back new virtual pages with physical memory.

Supports two modes:
- **Eager mapping** (`demand_paging = false`): frames are allocated immediately
  when `heap_malloc` grows the heap. Used with `app_load()`.
- **Demand paging** (`demand_paging = true`): virtual space is reserved but no
  frames are allocated. Frames are mapped lazily by `app_demand_alloc()` on the
  first access. Used with `app_load_bounded()`.

---

## Constants

| Constant               | Default | Description                              |
|------------------------|---------|------------------------------------------|
| `HEAP_ALIGN_BYTES`     | 16      | All allocations are rounded up to this   |
| `HEAP_MAX_ALLOCS`      | 256     | Maximum number of concurrent allocations |
| `HEAP_MAX_FREE_BLOCKS` | 256     | Maximum number of free-list entries      |

---

## Data Structures

### `heap_t`
The heap instance owned by each app.

| Field             | Type                   | Description                                        |
|-------------------|------------------------|----------------------------------------------------|
| `heap_base`       | `vmmu_vaddr_t`         | Start of the heap's virtual address range          |
| `heap_mapped_end` | `vmmu_vaddr_t`         | High-water mark: virtual memory committed so far   |
| `heap_limit`      | `vmmu_vaddr_t`         | Exclusive upper bound of the virtual range         |
| `free_blocks`     | `heap_free_block_t[]`  | Pool of free-list slot descriptors                 |
| `free_head`       | `int32_t`              | Index of the first free block, or -1               |
| `free_unused_head`| `int32_t`              | Head of the pool of unused slot descriptors        |
| `allocations`     | `heap_allocation_record_t[]` | Array tracking every live and freed allocation |
| `bytes_live`      | `size_t`               | Total bytes currently allocated                    |
| `bytes_mapped`    | `size_t`               | Total bytes backed by physical frames              |
| `demand_paging`   | `bool`                 | Whether physical allocation is deferred            |

### `heap_free_block_t`
One entry in the free list.

| Field   | Type          | Description                           |
|---------|---------------|---------------------------------------|
| `start` | `vmmu_vaddr_t`| Virtual address of this free block    |
| `size`  | `size_t`      | Size of the block in bytes            |
| `next`  | `int32_t`     | Index of the next free block, or -1   |

### `heap_allocation_record_t`
Tracks one allocation for size lookup and double-free detection.

| Field   | Type                     | Description                          |
|---------|--------------------------|--------------------------------------|
| `base`  | `vmmu_vaddr_t`           | Base address of the allocation       |
| `size`  | `size_t`                 | Rounded-up size in bytes             |
| `state` | `heap_allocation_state_t`| `UNUSED`, `LIVE`, or `FREED`         |
| `tag`   | `uint32_t`               | Reserved for future debugging        |

### `heap_status_t`
Return codes for all heap operations.

| Code                                  | Meaning                                      |
|---------------------------------------|----------------------------------------------|
| `HEAP_OK`                             | Success                                      |
| `HEAP_ERROR_OUT_OF_VIRTUAL_SPACE`     | `heap_mapped_end + growth > heap_limit`      |
| `HEAP_ERROR_OUT_OF_FRAMES`            | `map_pages` callback failed (no free frames) |
| `HEAP_ERROR_ALLOCATION_TRACKING_FULL` | All 256 allocation records are in use        |
| `HEAP_ERROR_FREE_LIST_FULL`           | All 256 free-block slots are in use          |
| `HEAP_ERROR_INVALID_POINTER`          | No live allocation found at this address     |
| `HEAP_ERROR_DOUBLE_FREE`              | Pointer already freed                        |
| `HEAP_ERROR_CORRUPT_STATE`            | NULL argument or inconsistent heap state     |

---

## Functions

### `heap_init(heap, heap_base, heap_limit)`
Initialises a heap instance for the virtual range `[heap_base, heap_limit)`.
Sets `heap_mapped_end = heap_base` (nothing mapped yet), clears the
allocation tracking array, and initialises the free-block slot pool.
`demand_paging` defaults to `false`.

### `heap_malloc(heap, size, map_pages, map_context, page_size, out_pointer)`
Allocates `size` bytes (rounded up to `HEAP_ALIGN_BYTES`) and writes the
virtual base address to `*out_pointer`.

Algorithm:
1. Scan `allocations[]` for a free slot (`UNUSED` first, then `FREED`).
2. Best-fit search through the free list for a block `>= size`.
3. If no fit: grow `heap_mapped_end` by enough pages.
   - In eager mode: call `map_pages` to back the pages with physical frames.
   - In demand-paging mode: skip `map_pages`; reserve virtual space only.
4. If `remainder >= HEAP_ALIGN_BYTES`: split the block (shrink from the front).
   Otherwise: consume the block entirely and remove it from the free list.
5. Record the allocation and return `HEAP_OK`.

### `heap_free(heap, pointer)`
Frees the allocation at `pointer`.
- Finds the `LIVE` record with `base == pointer` (interior pointers are rejected).
- Marks the record as `FREED` and returns the block to the free list.
- Calls `coalesce()` to merge adjacent free blocks.
- Returns `HEAP_ERROR_DOUBLE_FREE` if the record is already `FREED`.

### `heap_get_alloc_size(heap, pointer, out_size)`
Looks up the size of the live allocation at `pointer` and writes it to
`*out_size`. Used by `vmmu_free()` to know how many shadow bytes to clear.
Returns `HEAP_ERROR_INVALID_POINTER` if no live allocation matches.

---

## Internal Functions

### `coalesce(heap)`
Scans all pairs of free blocks and merges any pair where one ends exactly
where the other begins. Repeats until no more merges are possible.
Called after every `heap_free` to keep fragmentation low.

### `slot_pop(head, blocks)` / `slot_push(head, blocks, idx)`
O(1) helpers for managing the free-block slot pool as a stack.
`slot_pop` removes and returns the head index; `slot_push` prepends `idx`.
