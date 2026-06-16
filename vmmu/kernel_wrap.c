
#include "app/app.h"
#include "boundary.h"

#include <stddef.h>
#include <zephyr/kernel.h>

extern int __real_k_msgq_put(struct k_msgq *q, const void *data,
                             k_timeout_t timeout);
extern int __real_k_msgq_get(struct k_msgq *q, void *data,
                             k_timeout_t timeout);
extern int __real_k_pipe_put(struct k_pipe *pipe, const void *data,
                             size_t bytes, size_t *bytes_written,
                             size_t min_xfer, k_timeout_t timeout);
extern int __real_k_pipe_get(struct k_pipe *pipe, void *data,
                             size_t bytes, size_t *bytes_read,
                             size_t min_xfer, k_timeout_t timeout);

int __wrap_k_msgq_put(struct k_msgq *q, const void *data,
                      k_timeout_t timeout)
{
    if(app_current())
    {
        return vmmu_msgq_put(q, (vpointer)(uintptr_t)data, timeout);
    }
    return __real_k_msgq_put(q, data, timeout);
}

int __wrap_k_msgq_get(struct k_msgq *q, void *data,
                      k_timeout_t timeout)
{
    if(app_current())
    {
        return vmmu_msgq_get(q, (vpointer)(uintptr_t)data, timeout);
    }
    return __real_k_msgq_get(q, data, timeout);
}

int __wrap_k_pipe_put(struct k_pipe *pipe, const void *data,
                      size_t bytes, size_t *bytes_written,
                      size_t min_xfer, k_timeout_t timeout)
{
    if(app_current())
    {
        return vmmu_pipe_put(pipe, (vpointer)(uintptr_t)data,
                             bytes, bytes_written, min_xfer, timeout);
    }
    return __real_k_pipe_put(pipe, data, bytes, bytes_written,
                             min_xfer, timeout);
}

int __wrap_k_pipe_get(struct k_pipe *pipe, void *data,
                      size_t bytes, size_t *bytes_read,
                      size_t min_xfer, k_timeout_t timeout)
{
    if(app_current())
    {
        return vmmu_pipe_get(pipe, (vpointer)(uintptr_t)data,
                             bytes, bytes_read, min_xfer, timeout);
    }
    return __real_k_pipe_get(pipe, data, bytes, bytes_read,
                             min_xfer, timeout);
}
