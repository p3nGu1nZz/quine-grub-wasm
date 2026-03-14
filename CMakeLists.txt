cmake_minimum_required(VERSION 3.20)
project(WasmQuineBootloader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── SDL3 ───────────────────────────────────────────────────────────────────────
# Locate SDL3 config file; prefer system installation but fall back to
# our external directory if the build script has populated it.  We avoid
# calling find_package() until we actually know a config file exists so
# the CMake language server won’t emit an error during IDE configuration.

# candidate search paths (system prefixes automatically included by CMake):
# previous versions stored SDL3 in external/SDL3/build; current build script
# installs the library into external/SDL3/linux/lib/cmake/SDL3.
set(_sdl3_external "${CMAKE_SOURCE_DIR}/external/SDL3/linux/lib/cmake/SDL3")

# first look for the config file directly
find_file(SDL3ConfigPath
    NAMES SDL3Config.cmake sdl3-config.cmake
    PATHS
        ${_sdl3_external}
        # you may add more platform-specific hints here if needed
)

if(SDL3ConfigPath)
    get_filename_component(SDL3_DIR "${SDL3ConfigPath}" DIRECTORY)
    message(STATUS "Found SDL3 config at ${SDL3ConfigPath}")
    find_package(SDL3 REQUIRED CONFIG PATHS "${SDL3_DIR}")
else()
    # no configuration file found; the language server will not warn because
    # we never invoked find_package yet.  Build.sh will still attempt to set
    # SDL3_DIR and rerun CMake, or the user can run setup.sh to populate externals.
    message(WARNING "SDL3 not found; run scripts/setup.sh or install SDL3 system package")
endif()

# ── wasm3 ─────────────────────────────────────────────────────────────────────
set(WASM3_DIR "${CMAKE_SOURCE_DIR}/external/wasm3")
file(GLOB WASM3_SOURCES "${WASM3_DIR}/source/*.c")

# ── ImGui ─────────────────────────────────────────────────────────────────────
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui")
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
)

# ── Application sources ───────────────────────────────────────────────────────
## Core library containing non-UI application logic
set(CORE_SOURCES
    src/core/util.cpp
    src/core/base64.cpp
    src/core/log.cpp
    src/core/fsm.cpp
    src/core/exporter.cpp
    src/core/app.cpp
    src/wasm/kernel.cpp
    src/wasm/parser.cpp
    src/wasm/evolution.cpp
    src/core/cli.cpp
    src/nn/advisor.cpp
    src/nn/feature.cpp
    src/nn/policy.cpp
    src/nn/loss.cpp
    src/nn/train.cpp
    # wasm3 runtime objects are added to the core target below instead of
    # being inlined here; this avoids potential caching issues with the
    # CORE_SOURCES variable itself.
)

add_library(core STATIC ${CORE_SOURCES})
# wasm3 provides the runtime; compile its C sources into the core library so
# both the bootloader executable and unit tests resolve those symbols.
target_sources(core PRIVATE ${WASM3_SOURCES})
target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/nn
    "${WASM3_DIR}/source"
)
# SDL3 headers are needed by util.h and other core sources.  Export them so
# dependents (tests, executables) automatically inherit the include path and
# link library.
target_link_libraries(core PUBLIC SDL3::SDL3)

# ── Compiler warnings-as-errors ────────────────────────────────────────────
# Enable -Werror on GCC/Clang to keep the tree warning free.  The project
# already sets -Wall -Wextra in build scripts; this adds the error flag.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Werror)
endif()

# place all runtime outputs under a "bin" subdirectory to match
# the install layout and the user's expectations (see issue #...)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# Application executable (UI + platform glue)
set(APP_SOURCES
    src/main.cpp
    src/gui/window.cpp
    src/gui/panels.cpp
    src/gui/heatmap.cpp
)

add_executable(bootloader
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
    # wasm3 sources are now compiled into core
)

target_include_directories(bootloader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${WASM3_DIR}/source"
)

target_link_libraries(bootloader PRIVATE
    core
)

if(SDL3_FOUND)
    target_link_libraries(bootloader PRIVATE SDL3::SDL3)
endif()

# ── Windows (MinGW cross-compile) – static runtime to ship single .exe ────────
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_options(bootloader PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
    target_link_libraries(bootloader PRIVATE
        mingw32
        winmm
        ole32
        oleaut32
        imm32
        version
        setupapi
    )
endif()

# ── Compiler warnings ──────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # general warnings plus suppress some noisy unused-variable noise from
    # embedded sources (wasm3, etc.) which we don't control.
    target_compile_options(core PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
    )
    target_compile_options(bootloader PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
    )
endif()

# Enable testing and attempt to locate Catch2 (downloaded by setup.sh)
enable_testing()

# if Catch2 was installed by setup.sh, add its install dir to prefix path
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/Catch2/install")
find_package(Catch2 3.0.0 QUIET)
if(Catch2_FOUND)
    message(STATUS "Catch2 found; test targets will link against Catch2::Catch2")
else()
    message(STATUS "Catch2 not found; test targets may fail to configure")
endif()

add_subdirectory(test)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS bootloader RUNTIME DESTINATION bin)
