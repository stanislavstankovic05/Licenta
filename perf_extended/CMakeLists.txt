cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(perf_demo)

set(VMMU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vmmu)

option(PERF_VMMU "Build with VMMU instrumentation" OFF)
option(PERF_NO_TLB "Disable the TLB fast path (naive measurement)" OFF)
option(PERF_NO_HOIST "Use the no-hoist pass variant (per-element checks)" OFF)

# Both pass variants share the same PipelineStart+mem2reg registration and
# differ only in loop range hoisting — a clean A/B for the hoisting effect.
if(PERF_NO_HOIST)
    set(PASS_SO ${CMAKE_CURRENT_SOURCE_DIR}/../passes/build/libVMMUPassNoHoist.so)
else()
    set(PASS_SO ${CMAKE_CURRENT_SOURCE_DIR}/../passes/build/libVMMUPassO0.so)
endif()

# ----------------------------------------------------------------
# Common sources (always compiled with GCC)
# ----------------------------------------------------------------
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE src)

# ----------------------------------------------------------------
# VMMU build
# ----------------------------------------------------------------
if(PERF_VMMU)
    message(STATUS "[perf_demo] VMMU build — VMMUPass + shadow memory")

    target_sources(app PRIVATE
        src/vmmu_glue.c
        ${VMMU_DIR}/vmmu.c
        ${VMMU_DIR}/translator.c
        ${VMMU_DIR}/frame/frame.c
        ${VMMU_DIR}/virtual_space/addrspace.c
        ${VMMU_DIR}/virtual_space/vpage.c
        ${VMMU_DIR}/heap/heap.c
        ${VMMU_DIR}/app/app.c
        ${VMMU_DIR}/tlb.c
        ${VMMU_DIR}/libc_wrap.c
        ${VMMU_DIR}/pass_defaults.c
    )

    target_include_directories(app PRIVATE
        ${VMMU_DIR}
        ${VMMU_DIR}/frame
        ${VMMU_DIR}/virtual_space
        ${VMMU_DIR}/heap
        ${VMMU_DIR}/app
        ${VMMU_DIR}/bump
    )

    target_compile_definitions(app PRIVATE
        RAM_BASE_ADDR=0x20000000UL
        RAM_SIZE_BYTES=0x30000UL
        VMMU_NUM_FRAMES=8
        PERF_VMMU_BUILD
    )

    if(PERF_NO_TLB)
        message(STATUS "[perf_demo] TLB dezactivat (masuratoare naiva)")
        target_compile_definitions(app PRIVATE VMMU_DISABLE_TLB)
    endif()

    # malloc wrap routes app allocations through vmmu_malloc.
    # libc wraps route every memcpy/memset/strcpy/etc. through vmmu_check
    # before forwarding to picolibc — closes the libc bypass for app and
    # library code. Kernel IPC wraps (-Wl,--wrap=k_msgq_put ...) require
    # propagation into Zephyr's pre-link stage and remain future work.
    target_link_options(app PRIVATE
        -Wl,--wrap=malloc  -Wl,--wrap=free
        -Wl,--wrap=memcpy  -Wl,--wrap=memmove  -Wl,--wrap=memset
        -Wl,--wrap=memcmp
        -Wl,--wrap=strlen  -Wl,--wrap=strcpy   -Wl,--wrap=strncpy
        -Wl,--wrap=strcmp  -Wl,--wrap=strncmp
    )

    # workloads.c compiled with Clang + VMMUPass
    find_program(CLANG clang REQUIRED)
    set(WL_OBJ ${CMAKE_CURRENT_BINARY_DIR}/workloads.o)
    add_custom_command(
        OUTPUT  ${WL_OBJ}
        COMMAND ${CLANG}
                -target thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb
                -mfloat-abi=soft -fshort-enums -O2 -g
                -ffunction-sections -fdata-sections
                -fpass-plugin=${PASS_SO}
                -I${CMAKE_CURRENT_SOURCE_DIR}/src
                -c ${CMAKE_CURRENT_SOURCE_DIR}/src/workloads.c
                -o ${WL_OBJ}
        DEPENDS src/workloads.c ${PASS_SO}
        COMMENT "Clang + VMMUPass: workloads.c"
    )

# ----------------------------------------------------------------
# Bare build
# ----------------------------------------------------------------
else()
    message(STATUS "[perf_demo] bare build — no VMMU")

    # Bare build uses plain k_malloc — needs a Zephyr heap pool
    set(EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/bare.conf)

    target_sources(app PRIVATE src/bare_glue.c)

    # workloads.c compiled with plain Clang (no pass)
    find_program(CLANG clang REQUIRED)
    set(WL_OBJ ${CMAKE_CURRENT_BINARY_DIR}/workloads.o)
    add_custom_command(
        OUTPUT  ${WL_OBJ}
        COMMAND ${CLANG}
                -target thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb
                -mfloat-abi=soft -fshort-enums -O2 -g
                -ffunction-sections -fdata-sections
                -I${CMAKE_CURRENT_SOURCE_DIR}/src
                -c ${CMAKE_CURRENT_SOURCE_DIR}/src/workloads.c
                -o ${WL_OBJ}
        DEPENDS src/workloads.c
        COMMENT "Clang (bare): workloads.c"
    )
endif()

add_custom_target(workloads_obj DEPENDS ${WL_OBJ})
target_sources(app PRIVATE ${WL_OBJ})
set_source_files_properties(${WL_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_dependencies(app workloads_obj)
