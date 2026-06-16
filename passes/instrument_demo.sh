#!/bin/sh
# instrument_demo.sh — demonstrate VMMUPass on a standalone app-like file
#
# Compiles a minimal app function (same pattern as demo_apps.c) to LLVM IR
# with VMMUPass active. Shows the injected vmmu_check() calls in the output.
# No Zephyr headers needed — the pass works at IR level on any C code.
#
# Usage:
#   cd Licenta/passes && chmod +x instrument_demo.sh && ./instrument_demo.sh

set -e

CLANG=/opt/homebrew/opt/llvm/bin/clang
PLUGIN=./build/libVMMUPass.so

# Minimal self-contained app — same access patterns as the real demo
cat > /tmp/vmmu_app_test.c <<'EOF'
/*
 * Standalone app test — same memory access patterns as demo_apps.c.
 * No Zephyr headers needed; VMMUPass instruments at the IR load/store level.
 *
 * With VMMUPass: every pointer dereference below gets a vmmu_check() call
 * injected before it automatically. Without VMMUPass: no calls, silent access.
 */
#include <stdint.h>
#include <stddef.h>

/* VMMU interface — the pass injects calls to these */
void vmmu_check(void *ptr, size_t len, uint8_t access);
void *vmmu_malloc(size_t size);
void vmmu_free(void *ptr);

#define PERM_READ  1
#define PERM_WRITE 2

/* Simulates demo_oob_entry: allocate buf, then access past the end */
void demo_oob_entry(void)
{
    uint8_t *buf = (uint8_t *)vmmu_malloc(64);
    buf[0]  = 0xAA;            /* valid write — VMMUPass injects check before */
    buf[63] = 0xBB;            /* valid write — VMMUPass injects check before */
    buf[64] = 0xCC;            /* OOB write  — VMMUPass injects check before */
    vmmu_free(buf);
}

/* Simulates demo_subpage_oob_entry: malloc 20, access byte 22 */
void demo_subpage_oob_entry(void)
{
    uint8_t *buf = (uint8_t *)vmmu_malloc(20);
    buf[0]  = 1;               /* valid */
    buf[19] = 2;               /* valid — last owned byte */
    buf[22] = 3;               /* sub-page OOB — shadow byte is UNOWNED */
    vmmu_free(buf);
}

/* Simulates cross-app: read from address in another app's virtual range */
void demo_crossapp_entry(void)
{
    volatile uint32_t *cross = (uint32_t *)(9u * 4096u); /* vpage 9 */
    uint32_t val = *cross;  /* VMMUPass injects check — AS_ERR_UNMAPPED */
    (void)val;
}
EOF

echo "=== Compiling WITHOUT VMMUPass ==="
${CLANG} --target=arm-none-eabi -mcpu=cortex-m4 -mthumb \
    -O1 -S -emit-llvm /tmp/vmmu_app_test.c -o /tmp/without_pass.ll
BEFORE=$(grep -c "call" /tmp/without_pass.ll 2>/dev/null || echo 0)
echo "  call instructions in IR: ${BEFORE}"
echo "  vmmu_check calls:        0 (no pass)"

echo ""
echo "=== Compiling WITH VMMUPass ==="
${CLANG} --target=arm-none-eabi -mcpu=cortex-m4 -mthumb \
    -fpass-plugin=${PLUGIN} \
    -O1 -S -emit-llvm /tmp/vmmu_app_test.c -o /tmp/with_pass.ll
AFTER=$(grep -c "call.*vmmu_check" /tmp/with_pass.ll 2>/dev/null || echo 0)
echo "  vmmu_check calls injected: ${AFTER}"

echo ""
echo "=== Injected call sites (from with_pass.ll) ==="
grep -n "vmmu_check" /tmp/with_pass.ll | head -30

echo ""
echo "=== Summary ==="
echo "  VMMUPass injected ${AFTER} vmmu_check() calls automatically."
echo "  In demo_apps.c these were written by hand."
echo "  With full Zephyr clang toolchain, the pass runs on every app file."
echo ""
echo "Output files: /tmp/without_pass.ll  /tmp/with_pass.ll"
