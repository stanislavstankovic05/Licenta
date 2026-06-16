# frame — Physical Frame Allocator

Manages a fixed-size pool of physical memory frames using a singly-linked
free list. Each frame is `PAGE_SIZE` (4096) bytes. The allocator is O(1)
for alloc and free, and O(n) for reclaim.

---

## Data Structures

### `Frame`
Metadata for one physical frame.

| Field       | Type           | Description                                      |
|-------------|----------------|--------------------------------------------------|
| `state`     | `frame_state_t`| `FRAME_FREE` or `FRAME_USED`                     |
| `owner`     | `uint16_t`     | App ID that owns this frame, or sentinel value   |
| `flags`     | `uint16_t`     | Reserved for future use                          |
| `gen`       | `uint32_t`     | Generation counter (reserved)                    |
| `next_free` | `int32_t`      | Index of next frame in free list, or -1          |

### Constants
- `FRAME_OWNER_KERNEL` (`0xFFFF`) — frame owned by the kernel
- `FRAME_OWNER_NONE`   (`0xFFFE`) — frame is unowned / free

---

## Functions

### `frame_init(frame_pointer, frame_size, pool_start)`
Initialises the frame allocator with a pre-allocated array of `Frame`
structs and the physical base address of the backing memory pool.
Builds the free list by chaining all frames together.
`frame_paddr(i)` returns `pool_start + i * PAGE_SIZE`.

### `frame_alloc(owner)`
Pops the head of the free list and marks it as owned by `owner`.
Returns the frame index (non-negative) on success, or `-1` if:
- `owner` is `FRAME_OWNER_NONE`
- no free frames are available
- the free list is in an inconsistent state

### `frame_free(owner, frame_id)`
Returns frame `frame_id` to the free list.
The caller must be the owner, unless `owner == FRAME_OWNER_KERNEL`
(kernel can forcibly free any frame).
Returns `0` on success, `-1` on invalid input or ownership mismatch.

### `frame_paddr(frame_id)`
Converts a frame index to its physical base address in the pool.
Returns `0` for an out-of-range index (never a valid address).

### `frame_count_owner(owner)`
Counts how many frames are currently owned by `owner`.
Runs in O(n) over the full frame table.
Used by the demand pager to enforce the per-app `max_frames` budget.

### `frame_reclaim_owner(owner)`
Frees every frame owned by `owner`, returning them all to the free list.
Returns the number of frames reclaimed.
Called during `app_cleanup()` to release all resources of a terminated app.
