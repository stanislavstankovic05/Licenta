#include "app/app.h"
#include "translator.h"
#include <stddef.h>

extern void *__real_malloc(size_t size);
extern void __real_free(void *ptr);

void *__wrap_malloc(size_t size)
{
    if(app_current())
    {
        return vmmu_malloc(size);
    }
    return __real_malloc(size);
}

void __wrap_free(void *ptr)
{
    if(app_current())
    {
        vmmu_free(ptr);
        return;
    }
    __real_free(ptr);
}

void *__wrap__sbrk(int incr)
{
    (void)incr;
    return (void *)-1;
}
