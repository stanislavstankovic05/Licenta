#include "boundary.h"
#include "virtual_space/addrspace.h"
#include "virtual_space/vpage.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static uint8_t staging[VMMU_MAX_IPC_SIZE];
static struct k_mutex staging_mutex;

void boundary_init(void)
{
    k_mutex_init(&staging_mutex);
}

static boundary_status read_from_app(app *current_app,
                                     vpointer src,
                                     uint8_t *out,
                                     size_t len)
{
    size_t done = 0;

    while(done < len)
    {
        vpointer cur = src + done;
        size_t offset = cur % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - offset;
        if(chunk > len - done)
        {
            chunk = len - done;
        }

        uintptr_t paddr;
        int r = addrspace_translate(&current_app->address_space, (vpointer)cur, chunk,
                                    PERM_READ, &paddr);
        if(r != AS_OK)
        {
            printk("[vmmu] copy_from_app: fault vaddr=%p err=%d app=%u\n",
                   (void *)cur, r, (unsigned)current_app->id);
            return (r == AS_ERR_PERM) ? BOUNDARY_ERR_PERM
                                      : BOUNDARY_ERR_INVALID;
        }

        memcpy(out + done, (void *)paddr, chunk);
        done += chunk;
    }

    return BOUNDARY_OK;
}

static boundary_status write_to_app(app *current_app,
                                    const uint8_t *in,
                                    vpointer dst,
                                    size_t len)
{
    size_t done = 0;

    while(done < len)
    {
        vpointer cur = dst + done;
        size_t offset = cur % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - offset;
        if(chunk > len - done)
        {
            chunk = len - done;
        }

        uintptr_t paddr;
        int r = addrspace_translate(&current_app->address_space, (vpointer)cur, chunk,
                                    PERM_WRITE, &paddr);
        if(r != AS_OK)
        {
            printk("[vmmu] copy_to_app: fault vaddr=%p err=%d app=%u\n",
                   (void *)cur, r, (unsigned)current_app->id);
            return (r == AS_ERR_PERM) ? BOUNDARY_ERR_PERM
                                      : BOUNDARY_ERR_INVALID;
        }

        memcpy((void *)paddr, in + done, chunk);
        done += chunk;
    }

    return BOUNDARY_OK;
}

boundary_status copy_from_app(app *current_app,
                              vpointer src,
                              void *dst,
                              size_t len)
{
    if(!current_app || !dst)
    {
        return BOUNDARY_ERR_BADARG;
    }
    if(len > VMMU_MAX_IPC_SIZE)
    {
        return BOUNDARY_ERR_TOO_LARGE;
    }

    k_mutex_lock(&staging_mutex, K_FOREVER);

    boundary_status st = read_from_app(current_app, src, staging, len);
    if(st == BOUNDARY_OK)
    {
        memcpy(dst, staging, len);
    }

    k_mutex_unlock(&staging_mutex);

    if(st != BOUNDARY_OK)
    {
        app_abort();
    }

    return st;
}

boundary_status copy_to_app(app *current_app,
                            void *src,
                            vpointer dst,
                            size_t len)
{
    if(!current_app || !src)
    {
        return BOUNDARY_ERR_BADARG;
    }
    if(len > VMMU_MAX_IPC_SIZE)
    {
        return BOUNDARY_ERR_TOO_LARGE;
    }

    k_mutex_lock(&staging_mutex, K_FOREVER);

    memcpy(staging, src, len);
    boundary_status st = write_to_app(current_app, staging, dst, len);

    k_mutex_unlock(&staging_mutex);

    if(st != BOUNDARY_OK)
    {
        app_abort();
    }

    return st;
}

int vmmu_msgq_put(struct k_msgq *q,
                  vpointer app_msg,
                  k_timeout_t timeout)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return -EINVAL;
    }

    k_mutex_lock(&staging_mutex, K_FOREVER);

    boundary_status st = read_from_app(current_app, app_msg,
                                       staging, q->msg_size);
    if(st != BOUNDARY_OK)
    {
        k_mutex_unlock(&staging_mutex);
        app_abort();
        return -EFAULT;
    }

    int r = k_msgq_put(q, staging, timeout);

    k_mutex_unlock(&staging_mutex);
    return r;
}

int vmmu_msgq_get(struct k_msgq *q,
                  vpointer app_msg,
                  k_timeout_t timeout)
{
    app *current_app = app_current();
    if(!current_app)
    {
        return -EINVAL;
    }

    k_mutex_lock(&staging_mutex, K_FOREVER);

    int r = k_msgq_get(q, staging, timeout);
    if(r == 0)
    {
        boundary_status st = write_to_app(current_app, staging,
                                          app_msg, q->msg_size);
        if(st != BOUNDARY_OK)
        {
            k_mutex_unlock(&staging_mutex);
            app_abort();
            return -EFAULT;
        }
    }

    k_mutex_unlock(&staging_mutex);
    return r;
}

int vmmu_pipe_put(struct k_pipe *pipe,
                  vpointer app_data,
                  size_t bytes,
                  size_t *bytes_written,
                  size_t min_xfer,
                  k_timeout_t timeout)
{
    ARG_UNUSED(pipe);
    ARG_UNUSED(app_data);
    ARG_UNUSED(bytes);
    ARG_UNUSED(bytes_written);
    ARG_UNUSED(min_xfer);
    ARG_UNUSED(timeout);
    return -ENOSYS;
}

int vmmu_pipe_get(struct k_pipe *pipe,
                  vpointer app_data,
                  size_t bytes,
                  size_t *bytes_read,
                  size_t min_xfer,
                  k_timeout_t timeout)
{
    ARG_UNUSED(pipe);
    ARG_UNUSED(app_data);
    ARG_UNUSED(bytes);
    ARG_UNUSED(bytes_read);
    ARG_UNUSED(min_xfer);
    ARG_UNUSED(timeout);
    return -ENOSYS;
}
