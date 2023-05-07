
set(CMAKE_BUILD_TYPE Debug)     # Ensure debug info and assert functions are build during compile of tests


# --  CMake options  --
option(WITH_TEST_PROGRAMS "Build programs for testing tracing" ON)


# --  CMake targets  --
if (WITH_TEST_PROGRAMS)
   include_directories(../src/include/)

    # - Programs for testing tracing  -
    add_executable(trace_pthread_fork trace_pthread_fork.c)
    target_link_libraries(trace_pthread_fork pthread)

    add_executable(trace_descendants trace_descendants.c)
endif()
