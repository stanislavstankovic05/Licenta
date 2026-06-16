# vmmu — Root Module

The root module contains the core enforcement engine, shadow memory,
the software TLB, OS boundary APIs, and the malloc redirect layer.

---

## vmm.c / vmm.h — Physical Memory & Shadow Memory

Owns the two global static arrays that back all VMMU memory:
- `vmmu_frame_pool[VMMU_NUM_FRAMES * PAGE_SIZE]` — the physical frame pool
- `vmmu_shadow[VMMU_NUM_FRAMES * PAGE_SIZE]` — one shadow byte per pool byte

`VMMU_NUM_FRAMES` defaults to 64 and can be overridden via Kconfig.

Shadow byte values:
- `SHADOW_UNOWNED` (`0xFF`) — byte is unallocated, freed, or padding
- `0x00..APP_MAX-1` — byte is owned by that app ID

### `vmm_init()`
Bootstraps the VMMU. Calls `frame_init` to set up the free-list frame allocator
over `vmmu_frame_table` and `vmmu_frame_pool`, then `memset`s the entire shadow
array to `SHADOW_UNOWNED`. Prints a boot banner with pool addresses.
Must be called once at system startup before any app is loaded.

### `vmmu_in_pool(paddr)`
Returns `true` if `paddr` falls within the frame pool range.
Used by `vmmu_check` to decide whether to perform a shadow check —
stack and global addresses are outside the pool and are not shadow-tracked.

### `vmmu_shadow_mark(paddr, len, app_id)`
Marks `len` bytes starting at physical address `paddr` as owned by `app_id`.
Called by `vmmu_malloc` after a successful allocation, page by page,
to record exactly the requested number of bytes as owned (not the rounded-up
block size). This means padding bytes inside the block remain `SHADOW_UNOWNED`
and any access to them is caught as a sub-page OOB.

### `vmmu_shadow_clear(paddr, len)`
Resets `len` bytes at `paddr` back to `SHADOW_UNOWNED`.
Called by `vmmu_free` page by page before returning the allocation to the heap.
After this, any access to the freed range is caught by the shadow check
before the epoch check fires — providing byte-granular use-after-free detection.

### `vmmu_shadow_reclaim_owner(app_id)`
Scans the entire shadow array and resets every byte owned by `app_id` to
`SHADOW_UNOWNED`. Called by `app_cleanup()` before `frame_reclaim_owner()`
so that recycled frames start in a clean state for the next app.

### `vmmu_shadow_check(paddr, len, app_id)`
Checks every shadow byte in `[paddr, paddr+len)`.
- Returns `0`  — all bytes are owned by `app_id` (valid access).
- Returns `-1` — at least one byte is `SHADOW_UNOWNED` (sub-page OOB or use-after-free).
- Returns `-2` — at least one byte is owned by a different app (cross-app violation).
- Returns `0` immediately if `paddr` is outside the pool (not tracked).

---

## translator.c / translator.h — Enforcement Engine

The central enforcement layer. Every instrumented load/store passes through
`vmmu_check`. The file also owns `vmmu_malloc` and `vmmu_free`, which
coordinate page mapping, shadow marking, and TLB management.

### `vmmu_check(ptr, len, access)`
Called by VMMUPass before every IR load/store.

Fast path (TLB hit):
- Calls `tlb_lookup(vaddr, len, app_id)`. On hit, returns immediately.

Slow path (TLB miss):
1. `addrspace_translate` — validates all pages in `[ptr, ptr+len)` and
   returns the physical address of the first page.
2. If `AS_OK` and `vmmu_in_pool(paddr)`: shadow check page by page,
   re-translating for each page-aligned chunk to get the correct physical
   address per frame (frames are not contiguous).
3. If `AS_ERR_RANGE` or `AS_ERR_OVERFLOW`: the address is a physical RAM
   address (stack or global) outside the virtual space — let it through.
4. If `AS_ERR_UNMAPPED` and the address is inside `[heap_base, heap_limit)`:
   attempt demand paging via `app_demand_alloc`. If successful, return normally.
5. Any other error: print a fault report and call `app_abort()`.

Skipped entirely when called from a kernel thread (`app_current() == NULL`).

### `vmmu_stack_check()`
Injected by VMMUPass at the entry of every app function.
Reads the hardware SP register and compares it against
`k_thread->stack_info.start + VMMU_STACK_GUARD_MARGIN` (768 bytes).
If SP is below the margin, the app is aborted before any memory is corrupted.
The 768-byte margin accounts for the stack consumed by `app_abort` → `printk`
→ `k_thread_abort` → Zephyr scheduler internals.
Skipped in kernel context.

### `vmmu_globals_base()`
Called by VMMUPass-rewritten global variable accesses.
Returns the virtual base address of the current app's globals frame as a
`void*`. VMMUPass adds a compile-time offset to reach each specific variable.
Returns `NULL` in kernel context.

### `vmmu_malloc(size)`
Called via `--wrap=malloc` by the malloc redirect layer.
1. Calls `app_heap_malloc` to allocate `size` bytes from the app's heap.
2. Walks the virtual allocation page by page, calling `addrspace_translate`
   per chunk and `vmmu_shadow_mark` with the requested size (not the rounded-up
   size) to ensure padding bytes remain unowned.
3. Calls `tlb_insert` so future accesses to this allocation hit the TLB fast path.
Returns `NULL` if called from kernel context or if the heap is exhausted.

### `vmmu_free(ptr)`
Called via `--wrap=free` by the malloc redirect layer.
1. Calls `tlb_invalidate` so future accesses bypass the TLB and hit the full
   check path (enabling use-after-free detection).
2. Looks up the allocation size via `app_heap_get_alloc_size`.
3. Walks the virtual range page by page, re-translating per chunk and calling
   `vmmu_shadow_clear` for the full page to reset ownership.
4. Calls `app_heap_free` to return the block to the heap free list.

---

## tlb.c / tlb.h — Range-based Software TLB

An 8-entry direct-mapped cache where each entry covers an entire allocation
`[base, limit)` rather than a single 4 KB page. This makes a TLB hit independent
of allocation size — a 5000-byte buffer spanning two pages uses one entry.

Index function: `(base >> 12) & 7`

### `tlb_lookup(vaddr, len, app_id)` *(inline)*
Checks if `[vaddr, vaddr+len)` is inside a cached entry owned by `app_id`.
Returns `0` (hit — skip all further checks) or `-1` (miss).
Increments `g_tlb_hits` on a hit.

### `tlb_insert(base, size, app_id)` *(inline)*
Inserts an entry for allocation `[base, base+size)`.
Overwrites any existing entry at the same index (eviction without writeback).
Called by `vmmu_malloc` immediately after a successful allocation.

### `tlb_invalidate(base, app_id)` *(inline)*
Clears the entry at `tlb_index(base)` if it matches `base` and `app_id`.
Called by `vmmu_free` before freeing so that use-after-free accesses reach
the full check path.

### `tlb_flush_app(app_id)`
Scans all 8 entries and clears every entry owned by `app_id`.
Called by `app_cleanup()` to prevent stale entries from surviving into
the next use of the same app slot.

### `tlb_reset_stats()` *(inline)*
Resets the `g_tlb_hits` counter to zero. Used in demo/test code to get
a clean measurement of TLB effectiveness.

---

## boundary.c / boundary.h — OS Boundary APIs

Prevents IPC bypass: any kernel API that reads or writes app memory
must go through these wrappers. The kernel never touches raw app pointers.

All transfers use a single shared staging buffer `g_staging[VMMU_MAX_IPC_SIZE]`
protected by `g_staging_mutex`. On a single-core Cortex-M4 the mutex never
actually blocks, but makes the design correct for multi-core ports.

`VMMU_MAX_IPC_SIZE` defaults to 512 bytes and is configurable via Kconfig.

### `boundary_init()`
Initialises `g_staging_mutex`. Called once at boot from `vmm_init`.

### `copy_from_app(app, src, dst, len)`
Validates and copies `len` bytes from app virtual address `src` into kernel
buffer `dst`, one page-aligned chunk at a time. On any translation failure,
calls `app_abort()` and returns the error code.

### `copy_to_app(app, src, dst, len)`
Validates and copies `len` bytes from kernel buffer `src` into app virtual
address `dst`, one page-aligned chunk at a time. On any translation failure,
calls `app_abort()`.

### `vmmu_msgq_put(q, app_msg, timeout)`
Safe replacement for `k_msgq_put`. Validates and copies the message out of
app memory into the staging buffer via `read_from_app`, then calls the real
`k_msgq_put` with the staging buffer. The kernel never sees the app pointer.

### `vmmu_msgq_get(q, app_msg, timeout)`
Safe replacement for `k_msgq_get`. Reads from the queue into the staging
buffer, then writes to app memory via `write_to_app`.

### `vmmu_pipe_put(pipe, app_data, bytes, bytes_written, min_xfer, timeout)`
Safe replacement for `k_pipe_put`. Copies `bytes` from app memory into the
staging buffer, then calls `k_pipe_put` with the staging buffer.

### `vmmu_pipe_get(pipe, app_data, bytes, bytes_read, min_xfer, timeout)`
Safe replacement for `k_pipe_get`. Reads from the pipe into the staging
buffer, then copies to app memory. Only copies `*bytes_read` bytes
(the actual transfer size returned by the kernel).

---

## malloc_wrap.c — Malloc Redirect Layer

Transparent redirect of `malloc` / `free` / `_sbrk` via the linker
`--wrap` mechanism. Build with:
```
-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=_sbrk
```

### `__wrap_malloc(size)`
If the calling thread has an app context (`app_current() != NULL`),
redirects to `vmmu_malloc`. Otherwise falls back to `__real_malloc`
so kernel allocations are unaffected.

### `__wrap_free(ptr)`
If the calling thread has an app context, redirects to `vmmu_free`.
Otherwise falls back to `__real_free`.

### `__wrap__sbrk(incr)`
Always returns `(void*)-1` (failure). Prevents newlib from expanding
the heap behind the VMMU's back. All allocation must go through
`malloc_wrap` → `vmmu_malloc`.
