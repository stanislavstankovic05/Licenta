
#include "app/app.h"
#include "translator.h"
#include "virtual_space/vpage.h"
#include "vmmu.h"
#include <zephyr/kernel.h>

#define HEAP_BASE (1u * PAGE_SIZE)
#define HEAP_LIMIT (8u * PAGE_SIZE)

void perf_setup(void)
{
    vmmu_init();
    app_subsystem_init();

    app_load(0, NULL, HEAP_BASE, HEAP_LIMIT);
    app *current_app = app_get(0);
    current_app->state = APP_RUNNING;
    k_thread_custom_data_set(current_app);
}

const char *perf_label(void)
{
    return "WITH VMMU  (VMMUPass + shadow memory)";
}

void *__wrap_malloc(size_t n)
{
    return vmmu_malloc(n);
}
void __wrap_free(void *p)
{
    vmmu_free(p);
}
