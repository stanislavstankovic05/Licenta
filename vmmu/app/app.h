#ifndef APP_H
#define APP_H

#include "../heap/heap.h"
#include "../virtual_space/addrspace.h"
#include <stdint.h>
#include <zephyr/kernel.h>

#define APP_MAX 4
#define APP_STACK_SIZE 2048
#define APP_PRIORITY 7

typedef uint16_t app_id;

typedef enum
{
    APP_UNUSED = 0,
    APP_INIT,
    APP_RUNNING,
    APP_CRASHED,
    APP_STOPPED,
} app_state;

typedef enum
{
    APP_OK = 0,
    APP_ERR_INVALID_ID,
    APP_ERR_WRONG_STATE,
    APP_ERR_LOAD_FAILED,
} app_error;

typedef struct current_app
{
    app_id id;
    app_state state;
    addrspace address_space;
    heap heap;
    struct k_thread thread;
    k_thread_stack_t *stack;
    k_thread_entry_t entry;
    vpointer heap_base;
    vpointer heap_limit;
    uint32_t max_frames;
    vpointer globals_frame;
} app;

void app_subsystem_init(void);

int app_load(app_id id, k_thread_entry_t entry,
             vpointer heap_base, vpointer heap_limit);

int app_load_bounded(app_id id, k_thread_entry_t entry,
                     vpointer heap_base, vpointer heap_limit,
                     uint32_t peak_frames);

int app_start(app_id id);
void app_stop(app_id id);

void app_abort(void);
void app_cleanup(app_id id);

int app_restart(app_id id, k_thread_entry_t entry,
                vpointer heap_base, vpointer heap_limit);

app *app_current(void);
app *app_get(app_id id);

int app_demand_alloc(app *current_app, vpointer vaddr);

heap_status app_heap_malloc(app *current_app, size_t size, vpointer *out);
heap_status app_heap_free(app *current_app, vpointer ptr);
heap_status app_heap_get_alloc_size(app *current_app, vpointer ptr,
                                    size_t *out_size);

heap_status app_heap_find_alloc(app *current_app, vpointer addr,
                                vpointer *out_base, size_t *out_size);

#endif
