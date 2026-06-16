cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vmmu_demo)

set(VMMU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vmmu)

target_sources(app PRIVATE
    src/main.c
    src/demo_apps.c
    ${VMMU_DIR}/vmmu.c
    ${VMMU_DIR}/translator.c
    ${VMMU_DIR}/frame/frame.c
    ${VMMU_DIR}/virtual_space/addrspace.c
    ${VMMU_DIR}/virtual_space/vpage.c
    ${VMMU_DIR}/heap/heap.c
    ${VMMU_DIR}/app/app.c
    ${VMMU_DIR}/tlb.c
    ${VMMU_DIR}/pass_defaults.c
)

target_include_directories(app PRIVATE
    ${VMMU_DIR}
    ${VMMU_DIR}/frame
    ${VMMU_DIR}/virtual_space
    ${VMMU_DIR}/heap
    ${VMMU_DIR}/app
    ${VMMU_DIR}/bump
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# mps2/an386 QEMU target: SRAM at 0x20000000, 4MB
target_compile_definitions(app PRIVATE
    RAM_BASE_ADDR=0x20000000UL
    RAM_SIZE_BYTES=0x400000UL
    VMMU_NUM_FRAMES=64
)
