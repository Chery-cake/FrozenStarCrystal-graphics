# ==============================================================================
# BuildConfiguration.cmake - Debug and Release Build Configuration
# ==============================================================================
# Provides comprehensive build settings for Debug and Release configurations.
# - Debug: Debug symbols, unobfuscated code
# - Release: Full optimizations, LTO, stripped symbols
# ==============================================================================

# ==============================================================================
# Function to configure Debug build settings for a target
# ==============================================================================
function(target_configure_debug TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_debug: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Debug-specific compile options
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                -g3                     # Maximum debug information
                -O0                     # No optimization
                -fno-omit-frame-pointer # Keep frame pointers for better stack traces
                -fno-optimize-sibling-calls # Better stack traces
                -fstack-protector-strong # Stack overflow protection
            >
        )

        # Debug definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    elseif(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /Zi         # Debug information
                /Od         # No optimization
                /RTC1       # Runtime checks
                /GS         # Buffer security check
                /sdl        # Additional security checks
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to configure Release build settings for a target
# ==============================================================================
function(target_configure_release TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_release: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Release-specific compile options - maximum optimization
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                -O3                     # Maximum optimization
                -funroll-loops          # Unroll loops
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
                -fvisibility=hidden     # Hide symbols by default (obfuscation)
                -fvisibility-inlines-hidden # Hide inline function symbols
            >
        )

        # Optional: Optimize for current CPU (not suitable for distribution)
        if(ENGINE_OPTIMIZE_NATIVE)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -march=native       # Optimize for current CPU
                    -mtune=native       # Tune for current CPU
                >
            )
        endif()

        # Release definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for Release
        if(APPLE)
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,-dead_strip         # Remove unused code (Apple ld)
                >
            )
        else()
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,--gc-sections       # Remove unused sections
                    -Wl,--strip-all         # Strip all symbols
                    -Wl,-s                  # Strip symbol table
                >
            )
        endif()
    elseif(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /O2         # Maximum optimization
                /Ob2        # Inline expansion
                /Oi         # Intrinsic functions
                /Ot         # Favor fast code
                /GL         # Whole program optimization
                /Gy         # Function-level linking
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /LTCG       # Link-time code generation
                /OPT:REF    # Remove unreferenced code
                /OPT:ICF    # COMDAT folding
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to enable Link-Time Optimization (LTO) for Release builds
# ==============================================================================
function(target_enable_lto TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_lto: Target '${TARGET_NAME}' does not exist")
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

    if(lto_supported)
        set_target_properties(${TARGET_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        )
        message(STATUS "LTO enabled for ${TARGET_NAME} (Release builds)")
    else()
        message(WARNING "LTO not supported for ${TARGET_NAME}: ${lto_error}")
    endif()
endfunction()

# ==============================================================================
# Function to apply all build configurations to a target
# ==============================================================================
function(target_configure_build TARGET_NAME)
    set(options ENABLE_LTO)
    set(oneValueArgs "")
    set(multiValueArgs SANITIZERS)
    cmake_parse_arguments(CONFIG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_build: Target '${TARGET_NAME}' does not exist")
    endif()

    # Apply Debug configuration
    target_configure_debug(${TARGET_NAME})

    # Apply Release configuration
    target_configure_release(${TARGET_NAME})

    # Enable LTO if requested by the target AND the global option is enabled
    if(CONFIG_ENABLE_LTO AND ENGINE_ENABLE_LTO AND BUILD_SHARED_LIBS)
        target_enable_lto(${TARGET_NAME})
    endif()
endfunction()

# ==============================================================================
# Global Build Configuration Options
# ==============================================================================

# Release optimization options
option(ENGINE_OPTIMIZE_NATIVE "Optimize for current CPU (not suitable for distribution)" OFF)

# ==============================================================================
# Print Configuration Summary
# ==============================================================================
function(print_build_configuration)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Build Configuration")
    message(STATUS "========================================")
    message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS " LTO Enabled: ${ENGINE_ENABLE_LTO}")
    message(STATUS " Native Optimization: ${ENGINE_OPTIMIZE_NATIVE}")
    message(STATUS "========================================")
    message(STATUS "")
endfunction()
