# app — Application Lifecycle Manager

Manages up to `APP_MAX` (4) sandboxed applications. Each app has its own
address space, heap, Zephyr thread, and frame budget. The lifecycle is
controlled through a state machine.

---

## State Machine

```
UNUSED → INIT → RUNNING → STOPPED
                        → CRASHED
```

- **UNUSED**: slot available; no resources held.
- **INIT**: `app_load()` succeeded; thread not yet started.
- **RUNNING**: thread is executing.
- **STOPPED**: thread exited normally via the entry wrapper.
- **CRASHED**: `app_abort()` was called due to a memory fault.

Calling `app_load()` on a `CRASHED` or `STOPPED` slot triggers a lazy
`app_cleanup()` before reinitialising.

---

## Data Structures

### `app_t`
Per-app state. One instance per slot in `g_apps[APP_MAX]`.

| Field          | Type              | Description                                              |
|----------------|-------------------|----------------------------------------------------------|
| `id`           | `app_id_t`        | Slot index (0 to `APP_MAX - 1`)                          |
| `state`        | `app_state_t`     | Current lifecycle state                                  |
| `as`           | `addrspace`       | Virtual page table for this app                          |
| `heap`         | `heap_t`          | Per-app heap allocator state                             |
| `thread`       | `struct k_thread` | Zephyr thread control block                              |
| `stack`        | `k_thread_stack_t*` | Points into `g_app_stacks[id]`                        |
| `entry`        | `k_thread_entry_t`| The app's real entry function                            |
| `heap_base`    | `vmmu_vaddr_t`    | Start of the heap virtual range                          |
| `heap_limit`   | `vmmu_vaddr_t`    | Exclusive end of the heap virtual range                  |
| `max_frames`   | `uint32_t`        | Maximum frames this app may own simultaneously           |
| `globals_frame`| `vmmu_vaddr_t`    | Virtual base of the per-app globals frame (0 if unused)  |

---

## Functions

### `app_subsystem_init()`
Must be called once at boot before any other `app_*` function.
Initialises all `APP_MAX` slots to `APP_UNUSED` and assigns each its
pre-allocated stack from `g_app_stacks`.

### `app_load(id, entry, heap_base, heap_limit)`
Initialises slot `id` for execution.
- Triggers lazy `app_cleanup()` if the slot is `CRASHED` or `STOPPED`.
- Initialises the address space and heap for `[heap_base, heap_limit)`.
- Places a guard page at `heap_limit / PAGE_SIZE` with `perms=0`.
- Sets `max_frames` to the number of pages that fit in the heap range.
- Transitions state to `APP_INIT`.
- Fails with `APP_ERR_LOAD_FAILED` if `heap_base < PAGE_SIZE` (page 0 is
  the null page and must remain unmapped).

### `app_load_bounded(id, entry, heap_base, heap_limit, peak_frames)`
Like `app_load`, but additionally:
- Rejects at load time if `peak_frames > budget` (app can never fit).
- Sets `max_frames = min(budget, peak_frames)`.
- Enables demand paging (`heap.demand_paging = true`).
`peak_frames` is the static frame budget from `HeapBoundPass`.
Pass `UINT32_MAX` if the pass did not run — only the heap budget cap applies.

### `app_start(id)`
Creates the Zephyr thread and transitions the state to `APP_RUNNING`.
The thread runs `app_entry_wrapper`, which registers the app pointer as
thread-local data and allocates the globals frame before calling the real entry.
Requires the slot to be in `APP_INIT` state.

### `app_stop(id)`
Aborts the running thread via `k_thread_abort` and transitions to `APP_STOPPED`.
Only valid in `APP_RUNNING` state.

### `app_abort()`
Called from within the faulting app's thread (e.g. from `vmmu_check`).
Marks the current app as `APP_CRASHED`, prints a crash message, and calls
`k_thread_abort(k_current_get())`. Does not return.

### `app_cleanup(id)`
Releases all resources held by the app slot:
1. Flushes the TLB entries for this app.
2. Clears shadow memory for all bytes owned by this app.
3. Reclaims all physical frames owned by this app.
4. Bumps the address space epoch (stale-pointer detection).
5. Reinitialises the address space to the empty state.
6. Resets the globals frame pointer.
7. Transitions state to `APP_UNUSED`.

### `app_restart(id, entry, heap_base, heap_limit)`
Convenience wrapper: calls `app_load` followed by `app_start`.
`app_load` handles the lazy cleanup of a previous crashed/stopped run.

### `app_current()`
Returns the `app_t*` for the calling thread via `k_thread_custom_data_get()`.
Returns `NULL` when called from a kernel thread.
O(1) — no search required.

### `app_get(id)`
Returns a pointer to slot `id` without any state check.
Returns `NULL` for out-of-range IDs.

### `app_demand_alloc(app, vaddr)`
Allocates a physical frame for the unmapped virtual page containing `vaddr`
and inserts the mapping into the app's address space.
Called by `vmmu_check` on `AS_ERR_UNMAPPED` faults inside the heap range.
Enforces `app->max_frames` — returns `-1` if the budget is exhausted.

### `app_heap_malloc(app, size, out)`
Thin wrapper: calls `heap_malloc` with `map_pages` as the physical-frame
callback and writes the resulting virtual address to `*out`.

### `app_heap_free(app, ptr)`
Thin wrapper: calls `heap_free` for the allocation at virtual address `ptr`.

### `app_heap_get_alloc_size(app, ptr, out_size)`
Thin wrapper: looks up the allocation size for `ptr` via `heap_get_alloc_size`.
Used by `vmmu_free` to know how many shadow bytes to clear before freeing.

---

## Internal Functions

### `map_pages(ctx, vstart, npages)` *(static)*
The `heap_map_pages_fn` callback passed to `heap_malloc`.
Allocates `npages` physical frames via `frame_alloc` and maps them into the
app's address space starting at virtual address `vstart`.
Rolls back already-mapped frames if any `addrspace_map` call fails.

### `app_entry_wrapper(p1, p2, p3)` *(static)*
The actual Zephyr thread entry function for every app.
Registers the app pointer as thread-local custom data so `app_current()`
is O(1) from anywhere inside the thread.
If `__vmmu_globals_size > 0` (set by GlobalsPass), allocates the globals
frame and zeroes it before calling the real app entry.
Sets `state = APP_STOPPED` when the real entry returns.
