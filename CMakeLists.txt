cmake_minimum_required(VERSION 3.2)

project(clinfo)

# Requirements
set(CMAKE_C_STANDARD 99)
find_package(OpenCL REQUIRED)

set(SOURCE_FILE "src/clinfo.c")

set(HEADERS
  "src/error.h"
  "src/ext.h"
  "src/fmtmacros.h"
  "src/memory.h"
  "src/ms_support.h"
  "src/strbuf.h"
)

add_executable(clinfo "${SOURCE_FILE}" "${HEADERS}")

# Link OpenCL
include_directories(clinfo ${OpenCL_INCLUDE_DIR})
target_link_libraries(clinfo ${OpenCL_LIBRARY})

if(MSVC)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif(MSVC)

if(CMAKE_DL_LIBS)
  target_link_libraries(clinfo ${CMAKE_DL_LIBS})
endif()
