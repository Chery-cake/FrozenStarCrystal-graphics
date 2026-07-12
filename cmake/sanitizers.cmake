# cmake/sanitizers.cmake
include_guard(GLOBAL)

# ---- User-facing option ------------------------------------------------
# Set the SANITIZERS cache variable to a comma-separated list of sanitizers
# Example: cmake -B build -DSANITIZERS="address,undefined"
option(SANITIZERS
    "Sanitizers to enable (comma-separated): address, leak, thread, undefined, memory"
    "address,undefined"
)

# ==============================================================================
# Sanitizer Suppression File Path
# ==============================================================================

if(NOT DEFINED SANITIZER_SUPPRESSION_DIR)
    set(SANITIZER_SUPPRESSION_DIR "${CMAKE_SOURCE_DIR}/cmake/sanitizers" CACHE FILEPATH
        "Directory containing sanitizer suppression files")
endif()
if(NOT DEFINED ASAN_SUPPRESSION_FILE)
    set(ASAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/asan.supp" CACHE FILEPATH
        "Path to ASan suppression file")
endif()
if(NOT DEFINED LSAN_SUPPRESSION_FILE)
    set(LSAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/lsan.supp" CACHE FILEPATH
        "Path to LSan suppression file")
endif()

function(enable_sanitizers)
    if(NOT SANITIZERS)
        message(WARNING "No sanitizer enabled \n Example add: -DSANITIZERS=\"address,undefined\"")
        return()  # nothing to do
    endif()

    # Normalise the list
    string(REPLACE "," ";" _san_list "${SANITIZERS}")
    set(_san_lower_list)
    foreach(s IN LISTS _san_list)
        string(STRIP "${s}" s)
        string(TOLOWER "${s}" s_lower)
        list(APPEND _san_lower_list "${s_lower}")
    endforeach()

    # ---- Mutual exclusion checks ---------------------------------------
    if("address" IN_LIST _san_lower_list AND "thread" IN_LIST _san_lower_list)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be used together")
    endif()

    # (Optional) warn about redundant combinations
    if("address" IN_LIST _san_lower_list AND "leak" IN_LIST _san_lower_list)
        message(WARNING "AddressSanitizer already includes LeakSanitizer; 'leak' is redundant")
    endif()

    # ---- Build the -fsanitize flag -------------------------------------
    set(_san_flag_list)
    foreach(s IN LISTS _san_lower_list)
        if(s STREQUAL "address")
            list(APPEND _san_flag_list "address")
        elseif(s STREQUAL "leak")
            list(APPEND _san_flag_list "leak")
        elseif(s STREQUAL "undefined")
            list(APPEND _san_flag_list "undefined")
        elseif(s STREQUAL "thread")
            list(APPEND _san_flag_list "thread")
        elseif(s STREQUAL "memory")
            list(APPEND _san_flag_list "memory")
            message(STATUS "MemorySanitizer requires an instrumented standard library (e.g., libc++ with msan)")
        else()
            message(FATAL_ERROR "Unknown sanitizer: ${s}")
        endif()
    endforeach()
    string(REPLACE ";" "," _san_flag_str "${_san_flag_list}")

    # ---- Compiler/linker flags -----------------------------------------
    # ---- Remove any already‑present sanitizer flags (make idempotent) ----
    foreach(_flag_var CMAKE_C_FLAGS CMAKE_CXX_FLAGS
                    CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
        string(REGEX REPLACE "-fsanitize=[^ ]*" "" ${_flag_var} "${${_flag_var}}")
        string(REGEX REPLACE "-g -fno-omit-frame-pointer" "" ${_flag_var} "${${_flag_var}}")
        # Clean up multiple spaces
        string(STRIP "${${_flag_var}}" ${_flag_var})
    endforeach()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(_common "-g -fno-omit-frame-pointer")
        set(_san_flags "-fsanitize=${_san_flag_str}")

        # Add to all languages (C, CXX) and linker steps
        set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_common} ${_san_flags}"   PARENT_SCOPE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_common} ${_san_flags}" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${_san_flags}"    PARENT_SCOPE)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_san_flags}" PARENT_SCOPE)
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_san_flags}" PARENT_SCOPE)

        # Also set them in the cache so sub‑directories see them
        set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_common} ${_san_flags}"   CACHE STRING "" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_common} ${_san_flags}" CACHE STRING "" FORCE)
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${_san_flags}"    CACHE STRING "" FORCE)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_san_flags}" CACHE STRING "" FORCE)
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_san_flags}" CACHE STRING "" FORCE)

    elseif(MSVC)
        if("address" IN_LIST _san_lower_list)
            set(_msvc_san "/fsanitize=address")
            set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_msvc_san}"   PARENT_SCOPE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_msvc_san}" PARENT_SCOPE)
            set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${_msvc_san}"    PARENT_SCOPE)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_msvc_san}" PARENT_SCOPE)

            set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_msvc_san}"   CACHE STRING "" FORCE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_msvc_san}" CACHE STRING "" FORCE)
            set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${_msvc_san}"    CACHE STRING "" FORCE)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_msvc_san}" CACHE STRING "" FORCE)
        else()
            message(WARNING "MSVC only supports AddressSanitizer; ignoring other choices")
        endif()
    endif()

    # ---- Helper for test environment -----------------------------------
    # Store the enabled sanitizers in the cache so ctest can react.
    set(ENABLED_SANITIZERS "${_san_lower_list}" CACHE INTERNAL "Sanitizers active in this build")

    string(REPLACE ";" "," _san_flag_str "${_san_flag_list}")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=${_san_flag_str}>
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=${_san_flag_str}>
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=${_san_flag_str}>
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=${_san_flag_str}>
    )

    # Propagate suppression files into ASAN_OPTIONS / LSAN_OPTIONS for CTest.
    set(_target_env "")
    if("address" IN_LIST ENABLED_SANITIZERS AND ASAN_SUPPRESSION_FILE)
        list(APPEND _target_env "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:suppressions=${ASAN_SUPPRESSION_FILE}")
    elseif("address" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _target_env "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
    endif()
    if(("leak" IN_LIST ENABLED_SANITIZERS OR "address" IN_LIST ENABLED_SANITIZERS) AND LSAN_SUPPRESSION_FILE)
        list(APPEND _target_env "LSAN_OPTIONS=suppressions=${LSAN_SUPPRESSION_FILE}:print_suppressions=0")
    elseif("leak" IN_LIST ENABLED_SANITIZERS OR "address" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _target_env "LSAN_OPTIONS=print_suppressions=0")
    endif()
    if("undefined" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _target_env "UBSAN_OPTIONS=print_stacktrace=1")
    endif()
    if(_target_env)
        set_target_properties(${PROJECT_NAME} PROPERTIES ENVIRONMENT "${_target_env}")
    endif()

    message(STATUS "Sanitizers enabled ${_san_lower_list} on project ${PROJECT_NAME}")
endfunction()

function(enable_sanitizers_test)
    if(NOT SANITIZERS)
        message(WARNING "No sanitizer enabled \n Example add: -DSANITIZERS=\"address,undefined\"")
        return()  # nothing to do
    endif()

    # Normalise the list
    string(REPLACE "," ";" _san_list "${SANITIZERS}")
    set(_san_lower_list)
    foreach(s IN LISTS _san_list)
        string(STRIP "${s}" s)
        string(TOLOWER "${s}" s_lower)
        list(APPEND _san_lower_list "${s_lower}")
    endforeach()

    # ---- Mutual exclusion checks ---------------------------------------
    if("address" IN_LIST _san_lower_list AND "thread" IN_LIST _san_lower_list)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be used together")
    endif()

    # (Optional) warn about redundant combinations
    if("address" IN_LIST _san_lower_list AND "leak" IN_LIST _san_lower_list)
        message(WARNING "AddressSanitizer already includes LeakSanitizer; 'leak' is redundant")
    endif()

    # ---- Build the -fsanitize flag -------------------------------------
    set(_san_flag_list)
    foreach(s IN LISTS _san_lower_list)
        if(s STREQUAL "address")
            list(APPEND _san_flag_list "address")
        elseif(s STREQUAL "leak")
            list(APPEND _san_flag_list "leak")
        elseif(s STREQUAL "undefined")
            list(APPEND _san_flag_list "undefined")
        elseif(s STREQUAL "thread")
            list(APPEND _san_flag_list "thread")
        elseif(s STREQUAL "memory")
            list(APPEND _san_flag_list "memory")
            message(STATUS "MemorySanitizer requires an instrumented standard library (e.g., libc++ with msan)")
        else()
            message(FATAL_ERROR "Unknown sanitizer: ${s}")
        endif()
    endforeach()
    string(REPLACE ";" "," _san_flag_str "${_san_flag_list}")

    # ---- Helper for test environment -----------------------------------
    # Store the enabled sanitizers in the cache so ctest can react.
    set(ENABLED_SANITIZERS "${_san_lower_list}" CACHE INTERNAL "Sanitizers active in this build")

    string(REPLACE ";" "," _san_flag_str "${_san_flag_list}")
    target_compile_options(${TEST_EXE} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=${_san_flag_str}>
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=${_san_flag_str}>
    )
    target_link_options(${TEST_EXE} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=${_san_flag_str}>
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=${_san_flag_str}>
    )

    # Propagate suppression files into ASAN_OPTIONS / LSAN_OPTIONS for CTest.
    set(_test_env "")
    if("address" IN_LIST ENABLED_SANITIZERS AND ASAN_SUPPRESSION_FILE)
        list(APPEND _test_env "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:suppressions=${ASAN_SUPPRESSION_FILE}")
    elseif("address" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _test_env "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
    endif()
    if(("leak" IN_LIST ENABLED_SANITIZERS OR "address" IN_LIST ENABLED_SANITIZERS) AND LSAN_SUPPRESSION_FILE)
        list(APPEND _test_env "LSAN_OPTIONS=suppressions=${LSAN_SUPPRESSION_FILE}:print_suppressions=0")
    elseif("leak" IN_LIST ENABLED_SANITIZERS OR "address" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _test_env "LSAN_OPTIONS=print_suppressions=0")
    endif()
    if("undefined" IN_LIST ENABLED_SANITIZERS)
        list(APPEND _test_env "UBSAN_OPTIONS=print_stacktrace=1")
    endif()
    if(_test_env)
        set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "${_test_env}")
    endif()

    message(STATUS "Sanitizers enabled ${_san_lower_list} on test ${TEST_NAME}")
endfunction()
