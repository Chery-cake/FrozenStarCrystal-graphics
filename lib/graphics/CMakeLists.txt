# ==============================================================================
# Graphics Dependencies
# ==============================================================================
# - Vulkan - Explicit GPU API
# - VMA - GPU memory manager
# - SDL3 - Window/input/events
# - SDL_image - Image loading
# - SDL_ttf - TrueType fonts loading
# - shader-slang/slang - Runtime shader compilation
# ==============================================================================

include(FetchContent)
include(ExternalProject)

# ==============================================================================
# Vulkan SDK (headers only)
# ==============================================================================
#FetchContent_Declare(
#    vulkan_headers
#    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
#    GIT_TAG vulkan-sdk-1.4.350.1
#    GIT_SHALLOW TRUE
#)
#FetchContent_MakeAvailable(vulkan_headers)

FetchContent_Declare(
    vulkan_hpp
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
    GIT_TAG v1.4.352
    GIT_SHALLOW TRUE
)
set(VULKAN_HPP_DISPATCH_LOADER_DYNAMIC ON CACHE BOOL "Use dynamic loader")
set(VULKAN_HPP_ENABLE_SHARED_STORAGE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_hpp)

# Get the real (non-alias) targets for Vulkan-Hpp
#get_target_property(_vulkan_headers_real Vulkan::Headers ALIASED_TARGET)
get_target_property(_vulkan_hpp_real Vulkan::Hpp ALIASED_TARGET)
get_target_property(_vulkan_hpp_module_real Vulkan::HppModule ALIASED_TARGET)

# Propagate platform defines to the real targets so the module gets built with them
if(WIN32)
    #target_compile_definitions(${_vulkan_headers_real} INTERFACE VK_USE_PLATFORM_WIN32_KHR)
    target_compile_definitions(${_vulkan_hpp_real} INTERFACE VK_USE_PLATFORM_WIN32_KHR)
    target_compile_definitions(${_vulkan_hpp_module_real} INTERFACE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    #target_compile_definitions(${_vulkan_headers_real} INTERFACE VK_USE_PLATFORM_METAL_EXT)
    target_compile_definitions(${_vulkan_hpp_real} INTERFACE VK_USE_PLATFORM_METAL_EXT)
    target_compile_definitions(${_vulkan_hpp_module_real} INTERFACE VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX)
    #target_compile_definitions(${_vulkan_headers_real} INTERFACE VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
    target_compile_definitions(${_vulkan_hpp_real} INTERFACE VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
    target_compile_definitions(${_vulkan_hpp_module_real} INTERFACE VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
endif()

add_library(vk vulkan.cpp)
target_include_directories(vk PUBLIC
    ${vulkan_hpp_SOURCE_DIR}/Vulkan-Headers/include
    ${vulkan_hpp_SOURCE_DIR}/vulkan
)
target_link_libraries(vk PUBLIC
    #Vulkan::Headers
    Vulkan::Hpp
    Vulkan::HppModule
)
target_compile_definitions(vk PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_STORAGE_SHARED
)
#target_compile_definitions(vulkan PRIVATE
#  VULKAN_HPP_STORAGE_SHARED_EXPORT
#)
set_target_properties(vk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    C_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# ==============================================================================
# VMA - Vulkan Memory Allocator
# ==============================================================================
#FetchContent_Declare(
#    _vma
#    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
#    GIT_TAG v3.3.0
#    GIT_SHALLOW TRUE
#)
FetchContent_Declare(
    _vma_hpp
    GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
    GIT_TAG v3.3.0+3
    GIT_SHALLOW TRUE
)
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(_vma_hpp)

#get_target_property(_vma_hpp_module_real VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-HppModule ALIASED_TARGET)

add_library(vma vma.cpp)
target_link_libraries(vma PUBLIC
  vk
  GPUOpen::VulkanMemoryAllocator
  VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
  VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-HppModule
)
target_compile_definitions(vma PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)
# Suppress C4099 (class/struct type mismatch) from VMA HPP third-party headers
if(MSVC)
    target_compile_options(vma PRIVATE /wd4099)
endif()
set_target_properties(vma PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# ==============================================================================
# MoltenVK - Vulkan over Metal (macOS only)
# Fetches prebuilt binaries from GitHub releases — no build step needed.
# Creates a `moltenvk` INTERFACE target that the `vulkan` target links against.
# Also generates the ICD manifest so vk::detail::DynamicLoader finds it
# automatically without any hardcoded paths in C++ code.
# ==============================================================================
if(APPLE)
    FetchContent_Declare(
        _moltenvk
        URL https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(_moltenvk)

    set(MOLTENVK_DYLIB
        "${_moltenvk_SOURCE_DIR}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
    )
    set(MOLTENVK_INCLUDE
        "${_moltenvk_SOURCE_DIR}/MoltenVK/include"
    )

    # Interface target — same pattern as your slang/vma targets
    add_library(moltenvk INTERFACE)
    target_include_directories(moltenvk INTERFACE "${MOLTENVK_INCLUDE}")
    target_link_libraries(moltenvk INTERFACE "${MOLTENVK_DYLIB}")

    # Generate the ICD manifest pointing at the dylib
    # OUTPUT_DIR is already defined in your root CMakeLists.txt
    set(MOLTENVK_ICD_DIR "${OUTPUT_DIR}/vulkan/icd.d")
    file(MAKE_DIRECTORY "${MOLTENVK_ICD_DIR}")
    file(GENERATE
        OUTPUT  "${MOLTENVK_ICD_DIR}/MoltenVK_icd.json"
        CONTENT "{\n    \"file_format_version\": \"1.0.0\",\n    \"ICD\": {\n        \"library_path\": \"../../libMoltenVK.dylib\",\n        \"api_version\": \"1.3.0\"\n    }\n}\n"
    )

    # Copy the dylib itself to OUTPUT_DIR so the ICD relative path resolves
    add_custom_target(copy_moltenvk ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MOLTENVK_DYLIB}"
            "${OUTPUT_DIR}/libMoltenVK.dylib"
        COMMENT "[MoltenVK] Copying libMoltenVK.dylib to output"
    )

    target_link_libraries(vk PUBLIC moltenvk)
    add_dependencies(vk copy_moltenvk)
    message(STATUS "MoltenVK configured (macOS)")
endif()

# ==============================================================================
# shader-slang/slang - Runtime shader compiler - manual external project
# ==============================================================================
# Slang requires a two‑step build:
#   1. Build the host generator tools (slang-embed, etc.)
#   2. Build the library using SLANG_GENERATORS_PATH
# ==============================================================================

# ---------------------------------------------------------------------------
# Fetch the source once (do not update automatically)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    slang_source
    GIT_REPOSITORY   https://github.com/shader-slang/slang.git
    GIT_TAG          v2025.24.3
    GIT_SHALLOW      TRUE
    GIT_SUBMODULES_RECURSE FALSE   # needed by some internal libs
    GIT_PROGRESS     TRUE
    UPDATE_DISCONNECTED 1         # TODO update to a newer or better declaration
)

FetchContent_GetProperties(slang_source)
if(NOT slang_source_POPULATED)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
    FetchContent_Populate(slang_source)   # sets slang_source_SOURCE_DIR
    cmake_policy(POP)
endif()

set(SLANG_SOURCE_DIR ${slang_source_SOURCE_DIR})

set(SLANG_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/slang")
set(SLANG_GENERATORS_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/slang-generators")
if(WIN32 AND MSVC)
    set(_SLANG_BYPRODUCTS
        "${SLANG_INSTALL_DIR}/lib/slang.lib"
        "${SLANG_INSTALL_DIR}/bin/slang.dll"
    )
elseif(WIN32)
    # MinGW cross-compilation: import libraries use .dll.a suffix
    set(_SLANG_BYPRODUCTS
        "${SLANG_INSTALL_DIR}/lib/libslang.dll.a"
        "${SLANG_INSTALL_DIR}/bin/slang.dll"
    )
else()
    set(_SLANG_BYPRODUCTS
        "${SLANG_INSTALL_DIR}/lib/libslang${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
endif()

# TODO add toolchain for cross compiling

string(REGEX REPLACE "-fsanitize=[^ ]*" "" _SLANG_C_FLAGS   "${CMAKE_C_FLAGS}")
string(REGEX REPLACE "-fsanitize=[^ ]*" "" _SLANG_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "-fsanitize=[^ ]*" "" _SLANG_EXE_LINKER_FLAGS   "${CMAKE_EXE_LINKER_FLAGS}")
string(REGEX REPLACE "-fsanitize=[^ ]*" "" _SLANG_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
string(REGEX REPLACE "-fsanitize=[^ ]*" "" _SLANG_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" _SLANG_C_FLAGS   "${_SLANG_C_FLAGS}")
string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" _SLANG_CXX_FLAGS "${_SLANG_CXX_FLAGS}")
string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" _SLANG_EXE_LINKER_FLAGS   "${_SLANG_EXE_LINKER_FLAGS}")
string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" _SLANG_SHARED_LINKER_FLAGS "${_SLANG_SHARED_LINKER_FLAGS}")
string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" _SLANG_MODULE_LINKER_FLAGS   "${_SLANG_MODULE_LINKER_FLAGS}")

# Step 1: Build slang generator tools for the host platform
ExternalProject_Add(
    _slang_generators
    SOURCE_DIR   ${SLANG_SOURCE_DIR}
    BINARY_DIR   ${CMAKE_BINARY_DIR}/_deps/slang-generators-build
    INSTALL_DIR  ${SLANG_GENERATORS_INSTALL_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_FLAGS=${_SLANG_C_FLAGS}
        -DCMAKE_CXX_FLAGS=${_SLANG_CXX_FLAGS}
        -DCMAKE_EXE_LINKER_FLAGS=${_SLANG_EXE_LINKER_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${_SLANG_SHARED_LINKER_FLAGS}
        -DCMAKE_MODULE_LINKER_FLAGS=${_SLANG_MODULE_LINKER_FLAGS}
        -DSLANG_SLANG_LLVM_FLAVOR=DISABLE
        -DSLANG_ENABLE_SLANG_RHI=OFF
        -DSLANG_ENABLE_TESTS=OFF
        -DSLANG_ENABLE_EXAMPLES=OFF
        -DSLANG_ENABLE_GFX=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON

    BUILD_COMMAND
        ${CMAKE_COMMAND} --build <BINARY_DIR>
            --target all-generators
            --config "Release"

    INSTALL_COMMAND
        ${CMAKE_COMMAND} --install <BINARY_DIR>
            --prefix ${SLANG_GENERATORS_INSTALL_DIR}
            --component generators
            --config "Release"

    BUILD_BYPRODUCTS
        "${SLANG_GENERATORS_INSTALL_DIR}/bin/slang-embed${CMAKE_EXECUTABLE_SUFFIX}"
        # Add other generated executables if needed for dependency tracking:
        # "${SLANG_GENERATORS_INSTALL_DIR}/bin/slang-capability-generator${CMAKE_EXECUTABLE_SUFFIX}"
)

# Step 2: Build the actual slang library using the pre-built generators
ExternalProject_Add(
    _slang
    SOURCE_DIR   ${SLANG_SOURCE_DIR}
    BINARY_DIR   ${CMAKE_BINARY_DIR}/_deps/slang-build
    INSTALL_DIR  ${SLANG_INSTALL_DIR}
    DEPENDS      _slang_generators

    BUILD_BYPRODUCTS ${_SLANG_BYPRODUCTS}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_FLAGS=${_SLANG_C_FLAGS}
        -DCMAKE_CXX_FLAGS=${_SLANG_CXX_FLAGS}
        -DCMAKE_EXE_LINKER_FLAGS=${_SLANG_EXE_LINKER_FLAGS}
        -DCMAKE_SHARED_LINKER_FLAGS=${_SLANG_SHARED_LINKER_FLAGS}
        -DCMAKE_MODULE_LINKER_FLAGS=${_SLANG_MODULE_LINKER_FLAGS}
        -DCMAKE_INSTALL_PREFIX=${SLANG_INSTALL_DIR}
        -DSLANG_GENERATORS_PATH=${SLANG_GENERATORS_INSTALL_DIR}/bin
        -DSLANG_ENABLE_TESTS=OFF
        -DSLANG_ENABLE_EXAMPLES=OFF
        -DSLANG_ENABLE_GFX=OFF
        -DSLANG_ENABLE_SLANGD=OFF
        -DSLANG_ENABLE_SLANGC=ON
        -DSLANG_ENABLE_SLANGI=OFF
        -DSLANG_ENABLE_SLANGRT=ON
        -DSLANG_EXCLUDE_DAWN=ON
        -DSLANG_EXCLUDE_TINT=ON
        -DSLANG_SLANG_LLVM_FLAVOR=DISABLE
        -DSLANG_ENABLE_SLANG_RHI=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

add_library(slang INTERFACE)
add_dependencies(slang _slang)
#target_link_libraries(slang INTERFACE _slang)
target_include_directories(slang INTERFACE
    "${SLANG_INSTALL_DIR}/include"
)
# Link the actual library
if(WIN32 AND MSVC)
    target_link_libraries(slang INTERFACE
        "${SLANG_INSTALL_DIR}/lib/slang.lib"
    )
elseif(WIN32)
    # MinGW: use the .dll.a import library
    target_link_libraries(slang INTERFACE
        "${SLANG_INSTALL_DIR}/lib/libslang.dll.a"
    )
else()
    target_link_libraries(slang INTERFACE
        "${SLANG_INSTALL_DIR}/lib/libslang${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
endif()


# Copy slang shared libraries to the output directory so that test executables
# (and the main binary) can find transitive dependencies like
# libslang-compiler at runtime.  Uses cmake -E copy_directory which is
# cross-platform and avoids shell-escaping issues with ninja.
if(WIN32)
    add_custom_target(copy_slang_libs ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SLANG_INSTALL_DIR}/bin"
            "${OUTPUT_DIR}"
        COMMENT "[Slang] Copying DLLs to ${OUTPUT_DIR}"
    )
else()
    add_custom_target(copy_slang_libs ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SLANG_INSTALL_DIR}/bin"
            "${OUTPUT_DIR}/lib/"
        COMMENT "[Slang] Copying shared libraries to ${OUTPUT_DIR}/lib"
    )
endif()
add_dependencies(copy_slang_libs _slang)
add_dependencies(slang copy_slang_libs)

message(STATUS "Graphics dependencies configured")
