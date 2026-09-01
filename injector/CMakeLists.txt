cmake_minimum_required(VERSION 3.16)
project(ags_shader_injector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

add_library(ags-shader SHARED
    src/preload.cpp
    src/shader_pipeline_v2.cpp
)

target_include_directories(ags-shader PRIVATE src)
target_link_libraries(ags-shader PRIVATE PkgConfig::SDL2 OpenGL::GL dl)
target_compile_options(ags-shader PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(ags-shader PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "ags-shader"
)

add_executable(ags-shader-pipeline-test
    tests/test_pipeline.cpp
    src/shader_pipeline_v2.cpp
)
target_include_directories(ags-shader-pipeline-test PRIVATE src)
target_link_libraries(ags-shader-pipeline-test PRIVATE PkgConfig::SDL2 OpenGL::GL)
target_compile_options(ags-shader-pipeline-test PRIVATE -Wall -Wextra -Wpedantic)
