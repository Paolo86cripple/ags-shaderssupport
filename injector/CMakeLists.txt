cmake_minimum_required(VERSION 3.16)
project(ags_shader_injector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

set(AGS_SHADER_PIPELINE_SOURCES
    src/shader_pipeline.cpp
    src/shader_pipeline_v4.cpp
    src/preset_parser.cpp
    src/lut_manager.cpp
    src/ags_native_source_hook.cpp
)

# The v4 backend is compiled through four scoped compatibility/execution layers:
# 1. legacy GLSL/source and attribute compatibility;
# 2. RetroArch GL2 renderchain semantics/caches (POT FBO backing, UV ratios,
#    cached uniforms/texture parameters and classic global uniforms);
# 3. ScummVM-inspired static execution plans, moving auxiliary attribute/sampler
#    discovery out of the per-pass/per-frame render loop;
# 4. optional pass framebuffer diagnostics.
set_source_files_properties(
    src/shader_pipeline_v4.cpp
    PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gl_vertex_compat.h;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/retroarch_gl2_compat.h;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/scummvm_gl_exec_opt.h;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gl_pass_diagnostics_ra.h"
)

add_library(ags-shader SHARED
    src/preload.cpp
    ${AGS_SHADER_PIPELINE_SOURCES}
)
target_include_directories(ags-shader PRIVATE src)
target_link_libraries(ags-shader PRIVATE PkgConfig::SDL2 OpenGL::GL PNG::PNG JPEG::JPEG dl)
target_compile_options(ags-shader PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ags-shader PRIVATE GL_GLEXT_PROTOTYPES=1 AGS_SHADER_PRELOAD_BUILD=1)
set_target_properties(ags-shader PROPERTIES PREFIX "lib" OUTPUT_NAME "ags-shader")

add_executable(ags-shader-pipeline-test
    tests/test_pipeline.cpp
    ${AGS_SHADER_PIPELINE_SOURCES}
)
target_include_directories(ags-shader-pipeline-test PRIVATE src)
target_link_libraries(ags-shader-pipeline-test PRIVATE PkgConfig::SDL2 OpenGL::GL PNG::PNG JPEG::JPEG dl)
target_compile_options(ags-shader-pipeline-test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ags-shader-pipeline-test PRIVATE GL_GLEXT_PROTOTYPES=1)

add_executable(ags-shader-load-test
    tests/test_load.cpp
    ${AGS_SHADER_PIPELINE_SOURCES}
)
target_include_directories(ags-shader-load-test PRIVATE src)
target_link_libraries(ags-shader-load-test PRIVATE PkgConfig::SDL2 OpenGL::GL PNG::PNG JPEG::JPEG dl)
target_compile_options(ags-shader-load-test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ags-shader-load-test PRIVATE GL_GLEXT_PROTOTYPES=1)
