/* Minimal stubs so demo_apps.c compiles without Zephyr SDK */
#ifndef _STUBS_H
#define _STUBS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Zephyr printk → printf for IR emission purposes */
#define printk(fmt, ...) ((void)0)

/* Zephyr kernel types used by app.h / addrspace.h */
struct k_thread
{
    int dummy;
};
typedef struct k_thread *k_thread_stack_t;
typedef void (*k_thread_entry_t)(void *, void *, void *);
typedef uint16_t app_id_t;
#define K_MSEC(ms) (ms)
#define k_msleep(ms) ((void)0)
#define ARG_UNUSED(x) ((void)(x))

/* Forward-declare app_current so demo_apps.c compiles */
struct app;
struct app *app_current(void);

/* Zephyr types that appear in app.h via kernel.h */
#define CONFIG_THREAD_CUSTOM_DATA 1
typedef struct
{
    int d;
} addrspace;
#endif /* _STUBS_H */
