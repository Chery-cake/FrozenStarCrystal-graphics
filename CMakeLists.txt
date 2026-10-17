# ==============================================================================
# FrozenStarCrystal-graphics - Root CMakeLists.txt
# ==============================================================================
cmake_minimum_required(VERSION 4.0)

# ==============================================================================
# Bootstrap shared FSC tooling (FSCProject.cmake + friends)
# ==============================================================================
# The tooling lives in core's repo. We look for a sibling checkout first
# (fast, allows local edits), then fall back to downloading core's tarball
# into build/_fsc-tooling/.
#
# Override the search with:
#   -DFSC_CORE_DIR=/path/to/FrozenStarCrystal-core
# or the environment variable FSC_CORE_DIR.

set(FETCHCONTENT_QUIET FALSE CACHE BOOL
    "Show FetchContent/CPM download progress" FORCE)

# ==============================================================================
# Bootstrap shared FSC tooling
# ==============================================================================
# fsc_find_tooling() resolves the cmake/ directory containing FSCProject.cmake.
# It checks a sibling checkout first (fast, local edits), then falls back to
# downloading core's master tarball into build/_fsc-tooling/.
#
# Override the search with:
#   -DFSC_CORE_DIR=/path/to/FrozenStarCrystal-core
# or the environment variable FSC_CORE_DIR.

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FSCBootstrap.cmake")

fsc_find_tooling(_fsc_tooling_dir)
list(APPEND CMAKE_MODULE_PATH "${_fsc_tooling_dir}")
include(FSCProject)

project(FrozenStarCrystal-graphics
    VERSION 0.1.0
    LANGUAGES CXX C
)

fsc_initialize_project()

# Load developer overrides for local FSC package paths (optional file).
fsc_load_local_paths()

# ------------------------------------------------------------------------------
# Graphics API selection (project-specific knob)
# ------------------------------------------------------------------------------
if(DEFINED ENV{GRAPHICS_API})
    set(GRAPHICS_API "$ENV{GRAPHICS_API}" CACHE STRING
        "Select the graphics API (vulkan|opengl)" FORCE)
else()
    set(GRAPHICS_API "vulkan" CACHE STRING
        "Select the graphics API (vulkan|opengl)")
endif()

if(NOT GRAPHICS_API STREQUAL "vulkan")
    message(FATAL_ERROR "Unsupported GRAPHICS_API: ${GRAPHICS_API}")
endif()

# ------------------------------------------------------------------------------
# Dependencies (lib/ still does FetchContent for vk, vma, slang, core, ...)
# ------------------------------------------------------------------------------
add_subdirectory(lib)

# ------------------------------------------------------------------------------
# Sources: default src/ include/ plus the API-specific tree
# ------------------------------------------------------------------------------
file(GLOB_RECURSE API_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/src/*.c*"
)
list(FILTER API_SOURCES EXCLUDE REGEX "\\.(cppm|cc)$")

file(GLOB_RECURSE API_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/include/*.h*"
)

file(GLOB_RECURSE API_MODULES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/include/*.cppm"
)

# ------------------------------------------------------------------------------
# Build the module via the canonical entry point
# ------------------------------------------------------------------------------
add_module(
    EXTRA_SOURCES        ${API_SOURCES}
    EXTRA_PUBLIC_HEADERS ${API_PUBLIC_HEADERS}
    EXTRA_MODULES        ${API_MODULES}
    PUBLIC_DEPS
        FrozenStarCrystal-core
        vk
        vma
        slang
)

# ------------------------------------------------------------------------------
# App identity (project-specific, must come after add_module creates the target)
# ------------------------------------------------------------------------------
if(DEFINED ENV{APP_NAME})
    set(APP_NAME "$ENV{APP_NAME}")
else()
    set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name")
endif()

if(DEFINED ENV{APP_VERSION_MAJOR})
    set(APP_VERSION_MAJOR "$ENV{APP_VERSION_MAJOR}")
else()
    set(APP_VERSION_MAJOR 0 CACHE STRING "Application major version")
endif()
if(DEFINED ENV{APP_VERSION_MINOR})
    set(APP_VERSION_MINOR "$ENV{APP_VERSION_MINOR}")
else()
    set(APP_VERSION_MINOR 0 CACHE STRING "Application minor version")
endif()
if(DEFINED ENV{APP_VERSION_PATCH})
    set(APP_VERSION_PATCH "$ENV{APP_VERSION_PATCH}")
else()
    set(APP_VERSION_PATCH 0 CACHE STRING "Application major version")
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC
    APP_NAME="${APP_NAME}"
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR}
    APP_VERSION_MINOR=${APP_VERSION_MINOR}
    APP_VERSION_PATCH=${APP_VERSION_PATCH}
    GRAPHICS_BACKEND_VULKAN=1
)

# ------------------------------------------------------------------------------
# Assets
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/assets")
endif()

if(PROJECT_IS_TOP_LEVEL AND EXISTS "${CMAKE_SOURCE_DIR}/assets")
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${OUTPUT_DIR}/assets"
        COMMENT "Copying assets to build directory")
    add_dependencies(${PROJECT_NAME} copy_assets)
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
option(ENABLE_TESTS "Build tests" OFF)
option(FSC_TESTS    "Force enable tests" OFF)
if((ENABLE_TESTS AND PROJECT_IS_TOP_LEVEL) OR FSC_TESTS)
    add_subdirectory(tests)
endif()

fsc_finalize_top_level()
