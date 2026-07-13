# ==============================================================================
# Modular C++ Engine - Root CMakeLists.txt
# ==============================================================================

cmake_minimum_required(VERSION 4.0)

# Enable experimental import std support – UUID for CMake 4.3.2
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b"
    CACHE STRING "Enable import std experimental feature" FORCE
)

# Allow older third-party projects to be processed by CMake >= 4.0
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
endif()

enable_testing()

if(DEFINED ENV{CXX_MODULES_JSON})
    set(CMAKE_CXX_STDLIB_MODULES_JSON "$ENV{CXX_MODULES_JSON}" CACHE FILEPATH
        "Path to libc++.modules.json or libstdc++.modules.json (from environment)"
    )
    message(STATUS "Modules json included: $ENV{CXX_MODULES_JSON}")
endif()

project(FrozenStarCrystal-graphics
    VERSION 0.1.0
    LANGUAGES CXX C
)

# Enable Objective-C languages on macOS (required by SDL3)
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

# ==============================================================================
# Enforce Out-of-Source Build
# ==============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use out-of-source builds:\n"
                        "  cmake -B out -S .")
endif()

# ==============================================================================
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_MODULE_STD ON)

if(MSVC)
  set(CMAKE_C_STANDARD 17)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
else()
  set(CMAKE_C_STANDARD 23)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable POSIX features TODO check to remove this declaration
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Set output directories
set(TARGET_OS ${CMAKE_SYSTEM_NAME})
set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/${TARGET_OS})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# Force color diagnostics
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/diagnostics:color)
    # Force synchronous PDB writes as a safety net.
    add_compile_options(/FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pdb)
    # Use embedded debug info (/Z7) instead of separate PDB (/Zi).
    # sccache does not support PDB-based debug info, causing fatal error
    # C1041 when parallel cl.exe processes collide on shared vc*.pdb files.
    # /Z7 embeds debug info directly in .obj files, avoiding contention.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
    # Force CMP0141=NEW in FetchContent sub-projects that call
    # cmake_minimum_required with versions older than 3.25.  Without this,
    # those sub-projects revert to CMP0141=OLD and CMake injects /Zi
    # via its internal debug-info handling, ignoring the property above.
    set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
endif()

# ==============================================================================
# Custom CMake Modules
# ==============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(BuildConfiguration)
include(CrossCompiling)
include(AddEngineModule)

# ==============================================================================
# Dependencies
# ==============================================================================
add_subdirectory(lib)

# ==============================================================================
# Engine Subprojects
# ==============================================================================
#add_subdirectory(subprojects)

# ==============================================================================
# Main Application
# ==============================================================================
option(USING_API "Select the graphic api to use (vulkan|opengl)" "vulkan")

file(GLOB_RECURSE PRIVATE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*"

    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/src/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/src/*.h*"
)
list(FILTER PRIVATE_FILES EXCLUDE REGEX "\\.(cppm|cc)$")

file(GLOB_RECURSE PUBLIC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*"

    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/include/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/include/*.h*"
)
list(FILTER PUBLIC_FILES EXCLUDE REGEX "\\.(cppm|cc)$")

file(GLOB_RECURSE MODULES_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cppm"

    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/${USING_API}/include/*.cppm"
)

add_library(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE
        ${PRIVATE_FILES}
    PUBLIC
        ${PUBLIC_FILES}
)
target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET CXX_MODULES FILES
        ${MODULES_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    FrozenStarCrystal-core
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4251>
)

# Add ENGINE_DEBUG definition for Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# --- Application identity ---
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name" FORCE)
set(APP_VERSION_MAJOR 0 CACHE STRING "Application major version" FORCE)
set(APP_VERSION_MINOR 0 CACHE STRING "Application minor version" FORCE)
set(APP_VERSION_PATCH 0 CACHE STRING "Application patch version" FORCE)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    APP_NAME="${APP_NAME}"
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR}
    APP_VERSION_MINOR=${APP_VERSION_MINOR}
    APP_VERSION_PATCH=${APP_VERSION_PATCH}
)

if(USING_API STREQUAL "vulkan")
    target_compile_definitions(${PROJECT_NAME} PUBLIC GRAPHICS_BACKEND_VULKAN=1)
    target_link_libraries(${PROJECT_NAME} PUBLIC vma)
else()
    message(FATAL_ERROR "Unsupported USING_API: ${USING_API}")
endif()

create_export(${PROJECT_NAME} MODULES ${MODULES_FILES})

# ==============================================================================
# Apply Build Configuration
# ==============================================================================
target_configure_build(${PROJECT_NAME}
    ENABLE_LTO
    ENABLE_SANITIZERS
)

# Apply platform-specific configuration
target_configure_platform(${PROJECT_NAME})
target_configure_rpath(${PROJECT_NAME})

# ==============================================================================
# Tests
# ==============================================================================
option(ENABLE_TESTS "Add and Compile the project tests" OFF)

if(ENABLE_TESTS)
    include(sanitizers)
    enable_sanitizers()
    add_subdirectory(tests)
else()
    string(REGEX REPLACE "-fsanitize=[^ ]*" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "-fsanitize=[^ ]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "-fsanitize=[^ ]*" "" CMAKE_EXE_LINKER_FLAGS   "${CMAKE_EXE_LINKER_FLAGS}")
    string(REGEX REPLACE "-fsanitize=[^ ]*" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REGEX REPLACE "-fsanitize=[^ ]*" "" CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" CMAKE_EXE_LINKER_FLAGS   "${CMAKE_EXE_LINKER_FLAGS}")
    string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" CMAKE_MODULE_LINKER_FLAGS   "${CMAKE_MODULE_LINKER_FLAGS}")
endif()

# ==============================================================================
# Generate .clangd Configuration
# ==============================================================================
include(GenerateClangd)

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${OUTPUT_DIR}/assets DESTINATION share/engine)

# ==============================================================================
# Debug Output
# ==============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS " Modular C++ Engine v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS " C++ Compiler Standard: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS " C Standard: ${CMAKE_C_STANDARD}")
message(STATUS " C Compiler Standard: ${CMAKE_C_COMPILER_ID}")
message(STATUS " Output: ${OUTPUT_DIR}")
message(STATUS " Runtime:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS " Library: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "========================================")

# Print build configuration summary
print_build_configuration()

# Print cross-compilation info if applicable
print_cross_compilation_info()
