# T5 — LLVM Passes Implementation Notes

## VMMUPass.cpp

Runs as a **function pass** at `OptimizerLast` — after all optimisations, so the compiler can't eliminate checks it thinks are dead.

For every `LoadInst` and `StoreInst`:
1. Gets the pointer operand
2. Gets the byte size from the IR type via `DataLayout::getTypeStoreSize`
3. Injects `vmmu_check(ptr, size, PERM_READ/PERM_WRITE)` before the instruction

Functions starting with `vmmu_`, `app_`, `frame_`, `heap_`, `addrspace_`, `bump_` are skipped — kernel side, trusted.

---

## HeapBoundPass.cpp

Runs as a **module pass** at `PipelineStart` — before VMMUPass, needs the full module to see the call graph.

Three analysis steps:
1. **Call graph** — scans all `CallInst` in every function, builds an adjacency map, DFS detects cycles → `has_recursion`
2. **Malloc scan** — for each `malloc`/`vmmu_malloc`/`k_malloc` call: checks if size is a `ConstantInt`, accumulates `total_alloc_bytes`, checks if the call is inside a loop via `LoopInfo`
3. **Peak estimate** — conservative: `peak_live_bytes = total_alloc_bytes` (assumes all live at once)

Output goes into `!vmmu.bounds` IR metadata — survives into the `.bc`/`.ll` file and is readable at link time or by T9's `app_load()`.

---

## How they connect to translator.c

```
App source (.c)
    │
    ▼
HeapBoundPass  ──► !vmmu.bounds metadata ──► app_load() reads max_frames (T9)
    │
    ▼
VMMUPass       ──► injects vmmu_check() calls
    │
    ▼
translator.c   ──► vmmu_check() → addrspace_translate() → app_abort() on fault
                   vmmu_malloc() → app_heap_malloc() → heap_malloc() + map_pages
```

---

## Metadata format

```
!vmmu.bounds = !{!0}
!0 = !{
    i64  peak_live_bytes,   -- conservative peak simultaneous live bytes
    i64  total_alloc_bytes, -- sum of all constant malloc sizes seen
    i1   has_unknown_sizes, -- true if any malloc has a non-constant size
    i1   has_recursion,     -- true if the call graph contains a cycle
    i1   alloc_in_loop      -- true if any malloc is inside a loop
}
```

---

## Build

```bash
cd passes/
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
make
# produces: libVMMUPass.so, libHeapBoundPass.so
```

## Usage

```bash
clang -fpass-plugin=libHeapBoundPass.so \
      -fpass-plugin=libVMMUPass.so \
      app.c -o app.elf
```
