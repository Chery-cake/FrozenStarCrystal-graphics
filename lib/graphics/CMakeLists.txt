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

fsc_add_library(
    NAME _vulkan_hpp
    GITHUB_REPOSITORY KhronosGroup/Vulkan-Hpp
    GIT_TAG v1.4.352
    TEST_TARGET Vulkan::HppModule
    CPM_ARGS
        GIT_SHALLOW TRUE
        OPTIONS
            "VULKAN_HPP_DISPATCH_LOADER_DYNAMIC ON"
            "VULKAN_HPP_ENABLE_SHARED_STORAGE ON"
)

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
target_include_directories(vk SYSTEM PUBLIC
    ${_vulkan_hpp_SOURCE_DIR}/Vulkan-Headers/include
    ${_vulkan_hpp_SOURCE_DIR}/vulkan
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
fsc_add_library(
    NAME _vma_hpp
    GITHUB_REPOSITORY YaaZ/VulkanMemoryAllocator-Hpp
    GIT_TAG v3.4.0+1
    TEST_TARGET VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-HppModule
    CPM_ARGS
        GIT_SHALLOW TRUE
        OPTIONS
            "VMA_STATIC_VULKAN_FUNCTIONS OFF"
            "VMA_DYNAMIC_VULKAN_FUNCTIONS ON"
)

#get_target_property(_vma_hpp_module_real VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-HppModule ALIASED_TARGET)

add_library(vma vma.cpp)
target_include_directories(vma SYSTEM PUBLIC
    "${_vma_hpp_SOURCE_DIR}/VulkanMemoryAllocator/include"
    "${_vma_hpp_SOURCE_DIR}/include"
)
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
    CPMAddPackage(
        NAME _moltenvk
        URL https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

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
# Slang is built via ExternalProject_Add because it has a two-stage build
# (host generators first, then the library). That install step produces a
# portable include/+lib/ tree, which means a completed build in ANY sibling
# FSC checkout can be reused verbatim — no source fetch, no recompile.
#
# Resolution order:
#   1. Any registered sibling with build/_deps/slang/include/slang.h
#   2. This project's own build/_deps/slang (from a previous build)
#   3. Build it locally (populates build/_deps/slang for future projects)
# ==============================================================================

# --- Prerequisite: slang generator tools (shareable install) -----------------
# These are host-only tools (slang-embed, ...). Their install prefix lives in
# build/_deps/slang-generators and is reused from any sibling FSC checkout.
fsc_external_install(
    NAME slang_generators
    GITHUB_REPOSITORY shader-slang/slang
    GIT_TAG v2025.24.3
    INSTALL_SUBDIR slang-generators
    INSTALL_HEADER bin/slang-embed${CMAKE_EXECUTABLE_SUFFIX}
    CPM_ARGS
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE FALSE
    EXTERNAL_PROJECT_ARGS
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DSLANG_SLANG_LLVM_FLAVOR=DISABLE
            -DSLANG_ENABLE_SLANG_RHI=OFF
            -DSLANG_ENABLE_TESTS=OFF
            -DSLANG_ENABLE_EXAMPLES=OFF
            -DSLANG_ENABLE_GFX=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_COMMAND
            ${CMAKE_COMMAND} --build <BINARY_DIR>
                --target all-generators --config Release
        INSTALL_COMMAND
            ${CMAKE_COMMAND} --install <BINARY_DIR>
                --prefix <INSTALL_DIR>
                --component generators --config Release
        BUILD_BYPRODUCTS
            "${CMAKE_BINARY_DIR}/_deps/slang-generators/bin/slang-embed${CMAKE_EXECUTABLE_SUFFIX}"
)

# --- Main slang library: reuse a sibling's install or build locally ----------
# Pass EXTERNAL_PROJECT_DEPENDS only when the generators actually built here;
# when reused from a sibling, FSC_slang_generators_TARGET is empty and the dep
# is unnecessary.
set(_slang_generators_dep_args "")
if(FSC_slang_generators_TARGET)
    list(APPEND _slang_generators_dep_args
        EXTERNAL_PROJECT_DEPENDS "${FSC_slang_generators_TARGET}")
endif()

fsc_add_library(
    NAME slang
    GITHUB_REPOSITORY shader-slang/slang
    GIT_TAG v2025.24.3
    INSTALL_SUBDIR slang
    INSTALL_HEADER include/slang.h
    CPM_ARGS
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE FALSE
    ${_slang_generators_dep_args}
    EXTERNAL_PROJECT_ARGS
        CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DSLANG_GENERATORS_PATH=${FSC_slang_generators_INSTALL_DIR}/bin
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

message(STATUS "Graphics dependencies configured")
