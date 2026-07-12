# ==============================================================================
# CrossCompiling.cmake - Cross-Compilation Configuration
# ==============================================================================
# Provides functions and configurations for cross-compiling to different
# target platforms from the host system.
#
# Supported targets:
#   - Linux (x86_64, aarch64, armv7)
#   - Windows (x86_64, i686) via MinGW
#   - macOS (x86_64, arm64) - requires macOS host or osxcross
# ==============================================================================

# ==============================================================================
# Cross-Compilation Detection
# ==============================================================================
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling: YES")
    message(STATUS "  Host System: ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message(STATUS "  Target System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
else()
    message(STATUS "Cross-compiling: NO (native build)")
endif()

# ==============================================================================
# Target Platform Options
# ==============================================================================
set(CROSS_TARGET_PLATFORM "" CACHE STRING "Target platform for cross-compilation")
set_property(CACHE CROSS_TARGET_PLATFORM PROPERTY STRINGS
    ""
    "linux-x86_64"
    "linux-aarch64"
    "linux-armv7"
    "windows-x86_64"
    "windows-i686"
    "macos-x86_64"
    "macos-arm64"
)

# ==============================================================================
# Toolchain File Generation
# ==============================================================================

# Function to generate a toolchain file for a specific target
function(generate_toolchain_file TARGET_PLATFORM OUTPUT_FILE)
    string(REPLACE "-" ";" PLATFORM_PARTS "${TARGET_PLATFORM}")
    list(GET PLATFORM_PARTS 0 TARGET_OS)
    list(GET PLATFORM_PARTS 1 TARGET_ARCH)

    # Determine settings based on target
    if(TARGET_OS STREQUAL "linux")
        set(SYSTEM_NAME "Linux")
        if(TARGET_ARCH STREQUAL "x86_64")
            set(TRIPLE "x86_64-linux-gnu")
            set(GCC_PREFIX "x86_64-linux-gnu")
        elseif(TARGET_ARCH STREQUAL "aarch64")
            set(TRIPLE "aarch64-linux-gnu")
            set(GCC_PREFIX "aarch64-linux-gnu")
        elseif(TARGET_ARCH STREQUAL "armv7")
            set(TRIPLE "arm-linux-gnueabihf")
            set(GCC_PREFIX "arm-linux-gnueabihf")
        else()
            message(FATAL_ERROR "Unsupported Linux architecture: ${TARGET_ARCH}")
        endif()

        file(WRITE "${OUTPUT_FILE}"
"# Auto-generated toolchain file for ${TARGET_PLATFORM}
set(CMAKE_SYSTEM_NAME ${SYSTEM_NAME})
set(CMAKE_SYSTEM_PROCESSOR ${TARGET_ARCH})

# Cross-compiler settings
set(CMAKE_C_COMPILER ${GCC_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${GCC_PREFIX}-g++)
set(CMAKE_AR ${GCC_PREFIX}-ar)
set(CMAKE_RANLIB ${GCC_PREFIX}-ranlib)
set(CMAKE_STRIP ${GCC_PREFIX}-strip)

# Target triple for Clang (alternative)
set(CROSS_COMPILE_TRIPLE ${TRIPLE})

# Sysroot (set this to your cross-compilation sysroot if needed)
# set(CMAKE_SYSROOT /path/to/sysroot)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config for cross-compilation
set(ENV{PKG_CONFIG_PATH} \"\")
set(ENV{PKG_CONFIG_LIBDIR} \"\${CMAKE_SYSROOT}/usr/lib/pkgconfig:\${CMAKE_SYSROOT}/usr/share/pkgconfig\")
set(ENV{PKG_CONFIG_SYSROOT_DIR} \"\${CMAKE_SYSROOT}\")
")

    elseif(TARGET_OS STREQUAL "windows")
        set(SYSTEM_NAME "Windows")
        if(TARGET_ARCH STREQUAL "x86_64")
            set(TRIPLE "x86_64-w64-mingw32")
            set(WINE_EMULATOR "wine64")
        elseif(TARGET_ARCH STREQUAL "i686")
            set(TRIPLE "i686-w64-mingw32")
            set(WINE_EMULATOR "wine")
        else()
            message(FATAL_ERROR "Unsupported Windows architecture: ${TARGET_ARCH}")
        endif()

        file(WRITE "${OUTPUT_FILE}"
"# Auto-generated toolchain file for ${TARGET_PLATFORM}
set(CMAKE_SYSTEM_NAME ${SYSTEM_NAME})
set(CMAKE_SYSTEM_PROCESSOR ${TARGET_ARCH})

# MinGW cross-compiler settings
set(CMAKE_C_COMPILER ${TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${TRIPLE}-windres)
set(CMAKE_AR ${TRIPLE}-ar)
set(CMAKE_RANLIB ${TRIPLE}-ranlib)
set(CMAKE_STRIP ${TRIPLE}-strip)

# Windows-specific settings
set(CMAKE_EXECUTABLE_SUFFIX \".exe\")
set(CMAKE_SHARED_LIBRARY_SUFFIX \".dll\")
set(CMAKE_STATIC_LIBRARY_SUFFIX \".a\")

# Search paths
set(CMAKE_FIND_ROOT_PATH /usr/${TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Wine for running Windows executables during build (if needed)
set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE_EMULATOR})
")

    elseif(TARGET_OS STREQUAL "macos")
        set(SYSTEM_NAME "Darwin")
        if(TARGET_ARCH STREQUAL "x86_64")
            set(OSX_ARCH "x86_64")
            set(OSX_DEPLOYMENT_TARGET "10.15")
        elseif(TARGET_ARCH STREQUAL "arm64")
            set(OSX_ARCH "arm64")
            set(OSX_DEPLOYMENT_TARGET "11.0")
        else()
            message(FATAL_ERROR "Unsupported macOS architecture: ${TARGET_ARCH}")
        endif()

        file(WRITE "${OUTPUT_FILE}"
"# Auto-generated toolchain file for ${TARGET_PLATFORM}
set(CMAKE_SYSTEM_NAME ${SYSTEM_NAME})
set(CMAKE_SYSTEM_PROCESSOR ${TARGET_ARCH})
set(CMAKE_OSX_ARCHITECTURES ${OSX_ARCH})
set(CMAKE_OSX_DEPLOYMENT_TARGET ${OSX_DEPLOYMENT_TARGET})

# For osxcross, set these to your toolchain paths
# set(CMAKE_C_COMPILER /path/to/osxcross/bin/o64-clang)
# set(CMAKE_CXX_COMPILER /path/to/osxcross/bin/o64-clang++)

# For native macOS cross-arch builds
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()

# Architecture flags
add_compile_options(-arch ${OSX_ARCH})
add_link_options(-arch ${OSX_ARCH})

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
")

    else()
        message(FATAL_ERROR "Unsupported target OS: ${TARGET_OS}")
    endif()

    message(STATUS "Generated toolchain file: ${OUTPUT_FILE}")
endfunction()

# ==============================================================================
# Function to setup cross-compilation for current project
# ==============================================================================
function(setup_cross_compilation)
    if(NOT CROSS_TARGET_PLATFORM)
        return()
    endif()

    set(TOOLCHAIN_DIR "${CMAKE_SOURCE_DIR}/cmake/toolchains")
    file(MAKE_DIRECTORY "${TOOLCHAIN_DIR}")

    set(TOOLCHAIN_FILE "${TOOLCHAIN_DIR}/${CROSS_TARGET_PLATFORM}.cmake")
    
    if(NOT EXISTS "${TOOLCHAIN_FILE}")
        generate_toolchain_file("${CROSS_TARGET_PLATFORM}" "${TOOLCHAIN_FILE}")
    endif()

    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Cross-Compilation Setup")
    message(STATUS "========================================")
    message(STATUS " Target: ${CROSS_TARGET_PLATFORM}")
    message(STATUS " Toolchain: ${TOOLCHAIN_FILE}")
    message(STATUS "")
    message(STATUS " To use this toolchain, configure with:")
    message(STATUS "   cmake -B build -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
    message(STATUS "========================================")
    message(STATUS "")
endfunction()

# ==============================================================================
# Cross-Compilation Helpers
# ==============================================================================

# Function to check if a cross-compilation toolchain is available
function(check_cross_toolchain TARGET_PLATFORM RESULT_VAR)
    string(REPLACE "-" ";" PLATFORM_PARTS "${TARGET_PLATFORM}")
    list(GET PLATFORM_PARTS 0 TARGET_OS)
    list(GET PLATFORM_PARTS 1 TARGET_ARCH)

    set(FOUND FALSE)

    if(TARGET_OS STREQUAL "linux")
        if(TARGET_ARCH STREQUAL "x86_64")
            find_program(CROSS_GCC x86_64-linux-gnu-gcc)
        elseif(TARGET_ARCH STREQUAL "aarch64")
            find_program(CROSS_GCC aarch64-linux-gnu-gcc)
        elseif(TARGET_ARCH STREQUAL "armv7")
            find_program(CROSS_GCC arm-linux-gnueabihf-gcc)
        endif()
        if(CROSS_GCC)
            set(FOUND TRUE)
        endif()
    elseif(TARGET_OS STREQUAL "windows")
        if(TARGET_ARCH STREQUAL "x86_64")
            find_program(CROSS_GCC x86_64-w64-mingw32-gcc)
        elseif(TARGET_ARCH STREQUAL "i686")
            find_program(CROSS_GCC i686-w64-mingw32-gcc)
        endif()
        if(CROSS_GCC)
            set(FOUND TRUE)
        endif()
    elseif(TARGET_OS STREQUAL "macos")
        # macOS cross-compilation typically requires osxcross or native macOS
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            set(FOUND TRUE)
        else()
            find_program(OSXCROSS o64-clang PATHS /usr/local/osxcross/bin)
            if(OSXCROSS)
                set(FOUND TRUE)
            endif()
        endif()
    endif()

    set(${RESULT_VAR} ${FOUND} PARENT_SCOPE)
endfunction()

# Function to list available cross-compilation targets
function(list_available_cross_targets)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Available Cross-Compilation Targets")
    message(STATUS "========================================")

    set(TARGETS
        "linux-x86_64"
        "linux-aarch64"
        "linux-armv7"
        "windows-x86_64"
        "windows-i686"
        "macos-x86_64"
        "macos-arm64"
    )

    foreach(target ${TARGETS})
        check_cross_toolchain(${target} available)
        if(available)
            message(STATUS "  [✓] ${target}")
        else()
            message(STATUS "  [ ] ${target} (toolchain not found)")
        endif()
    endforeach()

    message(STATUS "========================================")
    message(STATUS "")
endfunction()

# ==============================================================================
# Platform-Specific Configuration
# ==============================================================================

# Function to apply platform-specific settings
function(target_configure_platform TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_platform: Target '${TARGET_NAME}' does not exist")
    endif()

    # Windows-specific settings
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(${TARGET_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
        )
        
        # Link Windows-specific libraries
        target_link_libraries(${TARGET_NAME} PRIVATE
            ws2_32       # Winsock
            winmm        # Multimedia
        )
    endif()

    # Linux-specific settings
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${TARGET_NAME} PRIVATE
            _GNU_SOURCE
        )
        
        # Link pthread explicitly for cross-compilation
        find_package(Threads REQUIRED)
        target_link_libraries(${TARGET_NAME} PRIVATE Threads::Threads)
        
        # Link dl for dynamic loading
        target_link_libraries(${TARGET_NAME} PRIVATE ${CMAKE_DL_LIBS})
    endif()

    # macOS-specific settings
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_compile_definitions(${TARGET_NAME} PRIVATE
            _DARWIN_C_SOURCE
        )
        
        # Framework linking
        find_library(COCOA_FRAMEWORK Cocoa)
        find_library(IOKIT_FRAMEWORK IOKit)
        find_library(COREVIDEO_FRAMEWORK CoreVideo)
        
        if(COCOA_FRAMEWORK)
            target_link_libraries(${TARGET_NAME} PRIVATE ${COCOA_FRAMEWORK})
        endif()
        if(IOKIT_FRAMEWORK)
            target_link_libraries(${TARGET_NAME} PRIVATE ${IOKIT_FRAMEWORK})
        endif()
        if(COREVIDEO_FRAMEWORK)
            target_link_libraries(${TARGET_NAME} PRIVATE ${COREVIDEO_FRAMEWORK})
        endif()
    endif()
endfunction()

# ==============================================================================
# RPATH Configuration for Cross-Compiled Binaries
# ==============================================================================
function(target_configure_rpath TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_rpath: Target '${TARGET_NAME}' does not exist")
    endif()

    # Set RPATH for installed binaries
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(${TARGET_NAME} PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH_USE_ORIGIN TRUE
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set_target_properties(${TARGET_NAME} PROPERTIES
            INSTALL_RPATH "@executable_path/../lib"
            BUILD_WITH_INSTALL_RPATH FALSE
            MACOSX_RPATH TRUE
        )
    endif()
endfunction()

# ==============================================================================
# Print Cross-Compilation Summary
# ==============================================================================
function(print_cross_compilation_info)
    if(CMAKE_CROSSCOMPILING)
        message(STATUS "")
        message(STATUS "========================================")
        message(STATUS " Cross-Compilation Configuration")
        message(STATUS "========================================")
        message(STATUS " Host: ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}")
        message(STATUS " Target: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
        message(STATUS " C Compiler: ${CMAKE_C_COMPILER}")
        message(STATUS " C++ Compiler: ${CMAKE_CXX_COMPILER}")
        if(CMAKE_SYSROOT)
            message(STATUS " Sysroot: ${CMAKE_SYSROOT}")
        endif()
        if(CMAKE_TOOLCHAIN_FILE)
            message(STATUS " Toolchain File: ${CMAKE_TOOLCHAIN_FILE}")
        endif()
        message(STATUS "========================================")
        message(STATUS "")
    endif()
endfunction()
