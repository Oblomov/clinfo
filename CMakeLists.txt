cmake_minimum_required(VERSION 3.0)
find_package(OpenCL REQUIRED)

set(CLINFO_SOURCES src/error.h
    src/ext.h
    src/fmtmacros.h
    src/memory.h
    src/ms_support.h
    src/strbuf.h
    src/clinfo.c)

add_executable(clinfo ${CLINFO_SOURCES})
target_include_directories(clinfo PRIVATE ${OpenCL_INCLUDE_DIRS})
target_link_libraries(clinfo PRIVATE ${OpenCL_LIBRARIES} -ldl)