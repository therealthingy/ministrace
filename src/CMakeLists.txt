# --  Vars  --
set(HEADERS_PRIVATE_DIRS ${ministrace_SOURCE_DIR}/src/include/)

set(SOURCES
        include/common/str_utils.c
        trace/internal/arch/ptrace_utils.c
        trace/internal/ptrace_utils.c
        trace/internal/syscall_types.c
        trace/internal/syscalls.c
        trace/tracing.c
        cli.c
        main.c)

set(COMPILE_OPTIONS "")


# --  CMake options  --
set(LINUX_SRC_DIR "/usr/src/linux-5.19.0" CACHE STRING "Location of kernel source used to parse syscalls")
option(PRINT_COMPLETE_STRING_ARGS "Print complete string args (instead of shortening them for better performance)" OFF)
option(WITH_STACK_UNWINDING "Stack unwinding option -k" OFF)            # Requires libunwind-dev, libdw-dev -y & libiberty-dev

if (PRINT_COMPLETE_STRING_ARGS)
    list(APPEND COMPILE_OPTIONS
                "-DPRINT_COMPLETE_STRING_ARGS")
endif()

if (WITH_STACK_UNWINDING)
    list(APPEND SOURCES
            trace/internal/unwind.c)
    list(APPEND COMPILE_OPTIONS
            "-DWITH_STACK_UNWINDING")
    list(APPEND LINK_OPTIONS
            unwind-ptrace unwind-generic
            iberty
            dw)
endif()


# --  CMake targets  --
# - Parse syscalls + generate source -
set(GEN_SYSCALLS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../scripts/compile/gen_syscalls_table.py")
set(GEN_SYSCALLS_TARGET_DIR "${ministrace_BINARY_DIR}/src/generated/trace/")

find_package(PythonInterp 3.4 REQUIRED)
add_custom_command(
        COMMENT "Parse syscalls from kernel source and generate source files"
        DEPENDS ${GEN_SYSCALLS_SCRIPT}
        OUTPUT "${GEN_SYSCALLS_TARGET_DIR}/syscallents.c" "${GEN_SYSCALLS_TARGET_DIR}/syscallents.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_SYSCALLS_TARGET_DIR}
        COMMAND ${PYTHON_EXECUTABLE} ${GEN_SYSCALLS_SCRIPT} ${LINUX_SRC_DIR} ${GEN_SYSCALLS_TARGET_DIR})

list(APPEND HEADERS_PRIVATE_DIRS ${ministrace_BINARY_DIR}/src/generated/)
list(APPEND SOURCES ${GEN_SYSCALLS_TARGET_DIR}/syscallents.c)


add_executable(ministrace ${SOURCES})
target_include_directories(ministrace PRIVATE ${HEADERS_PRIVATE_DIRS})
target_compile_options(ministrace PRIVATE ${COMPILE_OPTIONS})
target_link_libraries(ministrace PRIVATE ${LINK_OPTIONS})
