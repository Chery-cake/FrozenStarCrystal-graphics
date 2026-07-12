# ==============================================================================
# CompilerWarnings.cmake - Per-Target Compiler Warning Configuration
# ==============================================================================
# Provides functions to apply consistent warning settings to targets.
# NEVER applies global compiler flags - always per-target.
# ==============================================================================

# Function to apply standard warnings to a target
function(target_set_warnings TARGET_NAME)
    set(options TREAT_WARNINGS_AS_ERRORS)
    set(oneValueArgs "")
    set(multiValueArgs "")
    cmake_parse_arguments(WARNINGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
        )
        
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${TARGET_NAME} PRIVATE
                -Wmisleading-indentation
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast
            )
        endif()
        
        if(WARNINGS_TREAT_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()
        
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4
            /wd4251  # STL types in DLL interface (safe with same toolchain)
            /w14242  # conversion
            /w14254  # bitwise operator
            /w14263  # function override
            /w14265  # class with virtual functions
            /w14287  # unsigned/negative
            /we4289  # loop control variable
            /w14296  # expression always false
            /w14311  # pointer truncation
            /w14545  # ill-formed comma expression
            /w14546  # function call without argument list
            /w14547  # operator before comma
            /w14549  # operator before comma
            /w14555  # expression has no effect
            /w14619  # pragma warning
            /w14640  # thread unsafe static
            /w14826  # conversion signed to unsigned
            /w14905  # wide string literal cast
            /w14906  # string literal cast
            /w14928  # illegal copy-initialization
        )
        
        if(WARNINGS_TREAT_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        endif()
    endif()
endfunction()

# Function to apply relaxed warnings (for third-party code)
function(target_set_relaxed_warnings TARGET_NAME)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE
            -w  # Suppress all warnings
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            /W0  # Suppress all warnings
        )
    endif()
endfunction()
