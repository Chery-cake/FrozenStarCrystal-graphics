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
    add_compile_options(/FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pdb)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
    set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
endif()

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

if(DEFINED $ENV{GRAPHICS_API})
    set(GRAPHICS_API "$ENV{GRAPHICS_API}" CACHE STRING "Select the graphic api to use (vulkan|opengl)" FORCE)
else()
    set(GRAPHICS_API "vulkan" CACHE STRING "Select the graphic api to use (vulkan|opengl)")
endif()

file(GLOB_RECURSE PRIVATE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*"

    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/src/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/src/*.h*"
)
list(FILTER PRIVATE_FILES EXCLUDE REGEX "\\.(cppm|cc)$")

file(GLOB_RECURSE PUBLIC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*"

    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/include/*.c*"
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/include/*.h*"
)
list(FILTER PUBLIC_FILES EXCLUDE REGEX "\\.(cppm|cc)$")

file(GLOB_RECURSE MODULES_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cppm"

    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/${GRAPHICS_API}/include/*.cppm"
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

# Add ENGINE_DEBUG definition for Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# --- Application identity ---

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
)

if(GRAPHICS_API STREQUAL "vulkan")
    target_compile_definitions(${PROJECT_NAME} PUBLIC GRAPHICS_BACKEND_VULKAN=1)
    target_link_libraries(${PROJECT_NAME} PUBLIC vk vma slang)
else()
    message(FATAL_ERROR "Unsupported GRAPHICS_API: ${GRAPHICS_API}")
endif()

target_create_export(${PROJECT_NAME} MODULES ${MODULES_FILES})

# ==============================================================================
# Apply Build Configuration
# ==============================================================================
target_configure_build(${PROJECT_NAME})

# Apply platform-specific configuration
target_configure_platform(${PROJECT_NAME})
target_configure_rpath(${PROJECT_NAME})

# ==============================================================================
# Tests
# ==============================================================================
option(ENABLE_TESTS "Add and Compile the project tests" OFF)
option(FSC_TESTS "Force enable tests" OFF)

if((ENABLE_TESTS AND PROJECT_IS_TOP_LEVEL) OR FSC_TESTS)
    add_subdirectory(tests)
endif()

# ==============================================================================
# Copy Assets to Build Directory
# ==============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/assets")
endif()

add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${OUTPUT_DIR}/assets
    COMMENT "Copying assets to build directory"
)

add_dependencies(${PROJECT_NAME} copy_assets)

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${OUTPUT_DIR}/assets DESTINATION share/engine)

if(PROJECT_IS_TOP_LEVEL)

    # ==============================================================================
    # Generate .clangd Configuration
    # ==============================================================================
    generateClangd()

    # ==============================================================================
    # Debug Output
    # ==============================================================================

    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Modular C++ Engine v${PROJECT_VERSION}")
    message(STATUS "========================================")
    message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS " Build Shared Libs: ${BUILD_SHARED_LIBS}")
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

endif()
