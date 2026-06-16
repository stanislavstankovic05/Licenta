#ifndef BOUNDARY_H
#define BOUNDARY_H

#include "app/app.h"
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#ifndef CONFIG_VMMU_MAX_IPC_SIZE
#define CONFIG_VMMU_MAX_IPC_SIZE 512
#endif

#define VMMU_MAX_IPC_SIZE CONFIG_VMMU_MAX_IPC_SIZE

typedef enum
{
    BOUNDARY_OK = 0,
    BOUNDARY_ERR_INVALID,
    BOUNDARY_ERR_TOO_LARGE,
    BOUNDARY_ERR_PERM,
    BOUNDARY_ERR_BADARG,
} boundary_status;

boundary_status copy_from_app(app *current_app,
                              vpointer src,
                              void *dst,
                              size_t len);

boundary_status copy_to_app(app *current_app,
                            void *src,
                            vpointer dst,
                            size_t len);

int vmmu_msgq_put(struct k_msgq *q,
                  vpointer app_msg,
                  k_timeout_t timeout);

int vmmu_msgq_get(struct k_msgq *q,
                  vpointer app_msg,
                  k_timeout_t timeout);

int vmmu_pipe_put(struct k_pipe *pipe,
                  vpointer app_data,
                  size_t bytes,
                  size_t *bytes_written,
                  size_t min_xfer,
                  k_timeout_t timeout);

int vmmu_pipe_get(struct k_pipe *pipe,
                  vpointer app_data,
                  size_t bytes,
                  size_t *bytes_read,
                  size_t min_xfer,
                  k_timeout_t timeout);

#endif
