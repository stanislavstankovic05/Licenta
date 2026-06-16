# T4 — App Lifecycle Implementation Notes

## What was implemented and why

**`K_THREAD_STACK_ARRAY_DEFINE`** — declares 4 stack buffers at compile time, each 2048 bytes, properly aligned for Cortex-M4. Each `g_apps[i].stack` is wired to `g_app_stacks[i]` in `app_subsystem_init`.

**`app_entry_wrapper`** — a thin wrapper around the real entry function. Its job is to call `k_thread_custom_data_set(app)` before the app runs, so `app_current()` becomes O(1) anywhere inside that thread. If the entry returns normally it marks the app `APP_STOPPED`.

**`map_pages` callback** — the heap calls this when it needs more virtual space backed by physical memory. It allocates a frame per page via `frame_alloc(app->id)` and maps it into the app's address space. The `app->id` is the frame owner — this is what `frame_reclaim_owner` uses during cleanup.

**Lazy cleanup in `app_load`** — if the slot is `CRASHED` or `STOPPED`, `app_cleanup` runs before loading. This is where demand paging will plug in naturally later — cleanup defers work until the slot is actually reused.

**`app_cleanup`** — calls `frame_reclaim_owner` (returns all frames to the pool), then `addrspace_init` (wipes the page table). Heap is intentionally not reset here — `heap_init` in `app_load` will set it with the new bounds.

**`app_abort`** — looks up the current app via `app_current()`, marks it `CRASHED`, prints a message, then calls `k_thread_abort(k_current_get())`. Does not return.

**`app_current`** — one line: `k_thread_custom_data_get()`. Zero overhead.
