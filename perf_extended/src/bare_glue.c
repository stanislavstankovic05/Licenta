
#include <zephyr/kernel.h>

void perf_setup(void)
{
}

const char *perf_label(void)
{
    return "BARE  (no VMMU, no instrumentation)";
}
