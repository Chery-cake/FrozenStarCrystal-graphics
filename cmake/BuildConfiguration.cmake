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
function(target_configure_debug)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Debug-specific compile options
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                -g3                     # Maximum debug information
                -O0                     # No optimization
                -fno-omit-frame-pointer # Keep frame pointers for better stack traces
                -fno-optimize-sibling-calls # Better stack traces
                -fstack-protector-strong # Stack overflow protection
            >
        )

        # Debug definitions
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    elseif(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /Zi         # Debug information
                /Od         # No optimization
                /RTC1       # Runtime checks
                /GS         # Buffer security check
                /sdl        # Additional security checks
            >
        )

        target_compile_definitions(${PROJECT_NAME} PRIVATE
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
function(target_configure_release)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Release-specific compile options - maximum optimization
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:
                -O3                     # Maximum optimization
                -funroll-loops          # Unroll loops
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
                -fvisibility=hidden     # Hide symbols by default (obfuscation)
                -fvisibility-inlines-hidden # Hide inline function symbols
            >
        )

        # Release definitions
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for Release
        if(APPLE)
            target_link_options(${PROJECT_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,-dead_strip         # Remove unused code (Apple ld)
                >
            )
        else()
            target_link_options(${PROJECT_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,--gc-sections       # Remove unused sections
                    -Wl,--strip-all         # Strip all symbols
                    -Wl,-s                  # Strip symbol table
                >
            )
        endif()
    elseif(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /O2         # Maximum optimization
                /Ob2        # Inline expansion
                /Oi         # Intrinsic functions
                /Ot         # Favor fast code
                /GL         # Whole program optimization
                /Gy         # Function-level linking
            >
        )

        target_compile_definitions(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        target_link_options(${PROJECT_NAME} PRIVATE
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
function(target_enable_lto)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

    if(lto_supported)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        )
        message(STATUS "LTO enabled for ${PROJECT_NAME} (Release builds)")
    else()
        message(WARNING "LTO not supported for ${PROJECT_NAME}: ${lto_error}")
    endif()
endfunction()

# ==============================================================================
# Function to apply all build configurations to a target
# ==============================================================================
function(configure_build)

    if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        # Apply Debug configuration
        target_configure_debug()
    else()
        # Apply Release configuration
        target_configure_release()
    endif()

    # Enable LTO if requested by the target AND the global option is enabled
    if(ENABLE_LTO)
        target_enable_lto()
    endif()

    if(SANITIZERS)
        enable_sanitizers()
    endif()

    if(WARNINGS_LEVEL EQUAL 0)
        target_set_no_warnings(${PROJECT_NAME})
    elseif(WARNINGS_LEVEL EQUAL 1)
        target_set_relaxed_warnings(${PROJECT_NAME})
    elseif(WARNINGS_LEVEL EQUAL 2)
        target_set_warnings(${PROJECT_NAME})
    else()
        message(WARNING "Warning level of ${WARNINGS_LEVEL} doesn't exist")
    endif()

endfunction()

# ==============================================================================
# Global Build Configuration Options
# ==============================================================================

option(ENABLE_LTO "Enable LTO" ON)

if(DEFINED $ENV{WARNINGS_LEVEL})
    set(WARNINGS_LEVEL "$ENV{WARNINGS_LEVEL}" CACHE STRING "Warnings Level 0-2" FORCE)
else()
    set(WARNINGS_LEVEL 2 CACHE STRING "Warnings Level 0-2")
endif()

# ==============================================================================
# Print Configuration Summary
# ==============================================================================
function(print_build_configuration)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Build Configuration")
    message(STATUS "========================================")
    message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS " LTO Enabled: ${ENABLE_LTO}")
    message(STATUS " Sanitizers Enabled: ${SANITIZERS}")
    message(STATUS " Warnings Level: ${WARNINGS_LEVEL}")
    message(STATUS "========================================")
    message(STATUS "")
endfunction()
