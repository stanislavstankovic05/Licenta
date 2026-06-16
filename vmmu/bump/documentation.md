# bump — Boot-time Bump Allocator

A minimal one-way allocator used during early initialisation to carve out
permanent kernel memory regions. It cannot free — once a block is allocated
it lives for the lifetime of the system.

---

## Functions

### `bump_init(base, limit)`
Initialises the allocator with the memory range `[base, limit)`.
Sets the internal cursor to `base` and records `limit` as the hard ceiling.
Must be called once before any `bump_alloc` call.

### `bump_alloc(size, align)`
Allocates `size` bytes aligned to `align` bytes (must be a power of two).
Advances the internal cursor past the allocated block.
Returns a pointer to the start of the allocation, or `NULL` if:
- `align` is not a power of two
- the allocation would exceed `limit`
- integer overflow occurs

### `bump_get_current()`
Returns the current value of the bump cursor — the next available address.
Useful for diagnostics or computing how much memory has been consumed.

### `bump_get_limit()`
Returns the upper bound of the allocator's memory range.
Used to check remaining capacity without performing an allocation.
