# smartOs: CMake convenience for using the kernel in another project.
# The kernel has no build logic of its own; kernel.mk carries the same list.
#
#   add_subdirectory(smartOs)
#   smartos_add_kernel(my_kernel
#       CFG_DIR      ${CMAKE_CURRENT_BINARY_DIR}/gen      # sos_cfg.h
#       PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/port)    # sos_platform_cfg.h
#   target_link_libraries(my_app PRIVATE my_kernel)
#
# Building smartOs on its own (option SMARTOS_EXAMPLES) builds the
# hand-written example on the host virtual-time platform.

cmake_minimum_required(VERSION 3.21)
project(smartOs VERSION 0.4.0 LANGUAGES C)

set(SMARTOS_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")
set(SMARTOS_KERNEL_SOURCES
    ${SMARTOS_ROOT}/kernel/src/sos_sched.c
    ${SMARTOS_ROOT}/kernel/src/sos_time.c
    ${SMARTOS_ROOT}/kernel/src/sos_event.c
    ${SMARTOS_ROOT}/kernel/src/sos_error.c
    ${SMARTOS_ROOT}/kernel/src/sos_bitmap.c
    ${SMARTOS_ROOT}/kernel/src/sos_ring.c
    ${SMARTOS_ROOT}/kernel/src/sos_supervise.c
    CACHE INTERNAL "")

# smartos_add_kernel(<name> CFG_DIR <dir> PLATFORM_DIR <dir>)
function(smartos_add_kernel name)
    cmake_parse_arguments(A "" "CFG_DIR;PLATFORM_DIR" "" ${ARGN})
    if(NOT A_CFG_DIR OR NOT A_PLATFORM_DIR)
        message(FATAL_ERROR "smartos_add_kernel: CFG_DIR and PLATFORM_DIR are required")
    endif()
    add_library(${name} STATIC ${SMARTOS_KERNEL_SOURCES})
    target_include_directories(${name}
        PUBLIC ${SMARTOS_ROOT}/kernel/inc ${SMARTOS_ROOT}/platform
               ${A_CFG_DIR} ${A_PLATFORM_DIR})
    set_target_properties(${name} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
endfunction()

option(SMARTOS_EXAMPLES "Build the host example when smartOs is the top-level project"
       ${PROJECT_IS_TOP_LEVEL})
if(SMARTOS_EXAMPLES)
    smartos_add_kernel(smartos_kernel_host
        CFG_DIR ${SMARTOS_ROOT}/examples/manual_cfg
        PLATFORM_DIR ${SMARTOS_ROOT}/platform/host)
    add_executable(manual_cfg
        ${SMARTOS_ROOT}/examples/manual_cfg/main.c
        ${SMARTOS_ROOT}/platform/host/host_platform.c)
    target_link_libraries(manual_cfg PRIVATE smartos_kernel_host)
endif()
