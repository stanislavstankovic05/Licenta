cmake_minimum_required(VERSION 3.20)
project(vmmu_passes)

# Find LLVM — uses the Homebrew installation
find_package(LLVM REQUIRED CONFIG)

message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM include: ${LLVM_INCLUDE_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

set(CMAKE_CXX_STANDARD 17)

# Both plugins are shared libraries loaded via -fpass-plugin=
# They must NOT link against libLLVM — the host clang already has it loaded.

add_library(VMMUPass SHARED VMMUPass.cpp)
target_compile_options(VMMUPass PRIVATE -fno-rtti)

# O0 variant — registers at PipelineStart + mem2reg so it runs at -O0.
add_library(VMMUPassO0 SHARED VMMUPassO0.cpp)
target_compile_options(VMMUPassO0 PRIVATE -fno-rtti)

# No-hoist variant — same as VMMUPassO0 but loop hoisting disabled (per-element baseline).
add_library(VMMUPassNoHoist SHARED VMMUPassNoHoist.cpp)
target_compile_options(VMMUPassNoHoist PRIVATE -fno-rtti)

add_library(HeapBoundPass SHARED HeapBoundPass.cpp)
target_compile_options(HeapBoundPass PRIVATE -fno-rtti)

add_library(GlobalsPass SHARED GlobalsPass.cpp)
target_compile_options(GlobalsPass PRIVATE -fno-rtti)

# On macOS, LLVM symbols are resolved from the host clang at plugin load
# time via dlopen — do not link libLLVM, use dynamic_lookup instead.
# On Linux this is a no-op (ELF allows undefined symbols in shared libs).
if(APPLE)
    target_link_options(VMMUPass      PRIVATE -undefined dynamic_lookup)
    target_link_options(VMMUPassO0    PRIVATE -undefined dynamic_lookup)
    target_link_options(VMMUPassNoHoist PRIVATE -undefined dynamic_lookup)
    target_link_options(HeapBoundPass PRIVATE -undefined dynamic_lookup)
    target_link_options(GlobalsPass   PRIVATE -undefined dynamic_lookup)
endif()

# Output: libVMMUPass.so, libVMMUPassO0.so, libHeapBoundPass.so, libGlobalsPass.so
set_target_properties(VMMUPass VMMUPassO0 VMMUPassNoHoist HeapBoundPass GlobalsPass PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
)
