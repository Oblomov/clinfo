cmake_minimum_required(VERSION 3.6)

if (DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif ()

set(n clinfo)
project(${n})

find_package(OpenCL REQUIRED)

add_executable(${n} src/${n}.c)
target_include_directories(${n} PUBLIC src)
target_include_directories(${n} PUBLIC ${OpenCL_INCLUDE_DIRS})
target_link_libraries(${n} ${OpenCL_LIBRARIES})

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${n})
