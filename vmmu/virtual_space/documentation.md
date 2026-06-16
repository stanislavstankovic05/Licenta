# virtual_space — Virtual Address Space & Page Descriptors

Defines the virtual page descriptor (`vpage`) and the per-app address space
(`addrspace`). Each app gets an independent flat array of 1024 virtual page
entries. Translation walks this array to resolve virtual → physical addresses.

---

## Data Structures (`vpage.h`)

### `vpage`
One virtual page entry.

| Field      | Type      | Description                                              |
|------------|-----------|----------------------------------------------------------|
| `present`  | `bool`    | Whether this virtual page has a physical mapping         |
| `frame_id` | `int32_t` | Index into the frame pool; `-1` for guard pages          |
| `perms`    | `uint8_t` | Permission bits: `PERM_READ`, `PERM_WRITE`, `PERM_EXECUTE` |
| `epoch`    | `uint32_t`| Epoch at which this mapping was created                  |

### Permission bits
- `PERM_READ    = 0b001`
- `PERM_WRITE   = 0b010`
- `PERM_EXECUTE = 0b100`

### Constants
- `PAGE_SIZE  = 4096` — bytes per page
- `VPAGE_NULL = 0`    — virtual page 0 is always the null page; never mapped

### `vptr`
Alias for `uintptr_t`. Used as the type for virtual address arguments
passed to address-space functions.

---

## Data Structures (`addrspace.h`)

### `addrspace`
Per-app address space.

| Field    | Type            | Description                                  |
|----------|-----------------|----------------------------------------------|
| `pages`  | `vpage[1024]`   | Flat virtual page table, indexed by vpage number |
| `epoch`  | `uint32_t`      | Current epoch; bumped on cleanup to invalidate all mappings |

### Error codes (`as_err`)
| Code              | Value | Meaning                                   |
|-------------------|-------|-------------------------------------------|
| `AS_OK`           | 0     | Success                                   |
| `AS_ERR_RANGE`    | -1    | Virtual page index out of bounds          |
| `AS_ERR_ALREADY`  | -2    | Page already mapped                       |
| `AS_ERR_UNMAPPED` | -3    | Page not present                          |
| `AS_ERR_PERM`     | -4    | Access permission denied                  |
| `AS_ERR_EPOCH`    | -5    | Stale pointer — mapping from a past epoch |
| `AS_ERR_OVERFLOW` | -6    | Address arithmetic overflowed             |
| `AS_ERR_BADARG`   | -7    | NULL pointer or invalid argument          |

---

## Functions

### `addrspace_init(as)`
Resets all 1024 page entries to the unmapped state: `present=false`,
`frame_id=-1`, `perms=0`, `epoch` set to the current address space epoch.
Called at `app_load()` and again at `app_cleanup()` after bumping the epoch.

### `addrspace_bump_epoch(as)`
Increments the address space epoch counter.
All existing page entries now have a stale epoch, so any access through
a cached pointer returns `AS_ERR_EPOCH` — use-after-free / stale-pointer detection.
Called by `app_cleanup()` before `addrspace_init()`.

### `addrspace_map(as, vp, frame_id, perms)`
Maps virtual page `vp` to physical frame `frame_id` with permissions `perms`.
Fails with `AS_ERR_ALREADY` if `vp` is already present.
Fails with `AS_ERR_RANGE` if `vp >= MAX_VPAGES` or `vp == VPAGE_NULL`.
Called by `map_pages()` (the heap's page-mapping callback) and by
`app_demand_alloc()` on demand faults.

### `addrspace_map_guard(as, vp)`
Maps virtual page `vp` as a guard page: `present=true`, `perms=0`, `frame_id=-1`.
Any access returns `AS_ERR_PERM` before `frame_paddr` is ever dereferenced.
Used by `app_load()` to place a guard page at `heap_limit / PAGE_SIZE`,
catching heap overflows at the virtual boundary.

### `addrspace_unmap(as, vp)`
Removes the mapping for virtual page `vp`.
Returns the `frame_id` that was mapped there (caller is responsible for freeing it),
or a negative error code.

### `addrspace_translate(as, ptr, len, access, out_paddr)`
Validates the entire range `[ptr, ptr+len)` and resolves the virtual address
to a physical address.

Steps:
1. Computes `start_page` and `end_page` from `ptr` and `ptr+len-1`.
2. Rejects if any page index is out of `[1, MAX_VPAGES)`.
3. For every page in `[start_page, end_page]`: checks `present`, `perms`, and `epoch`.
4. Returns the physical address of `ptr` within the first page's frame via `out_paddr`.

If the access spans multiple pages, only the physical address of the first page
is returned. Callers that need per-page physical addresses (shadow memory checks,
IPC copies) must re-call `addrspace_translate` for each page-aligned chunk.
