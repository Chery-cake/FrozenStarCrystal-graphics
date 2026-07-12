# cmake/AddEngineModule.cmake

function(create_export TARGET_NAME)
    # optional MODULES parameter, to pass module files of the target
    cmake_parse_arguments(ARG "" "" "MODULES" ${ARGN})


    # Assume the target already exists (add_library called before this)
    # Generate export header with standard naming:
    #   BASE_NAME = uppercase target name
    #   EXPORT_MACRO_NAME = <BASE_NAME>_API
    #   EXPORT_FILE_NAME = <target_name>_export.h
    string(TOUPPER ${TARGET_NAME} BASE_NAME_UPPER)
    string(REGEX REPLACE "-" "_" BASE_NAME_UPPER ${BASE_NAME_UPPER})
    set(EXPORT_MACRO_NAME "${BASE_NAME_UPPER}_API")
    set(EXPORT_HEADER_NAME "${TARGET_NAME}_export.h")

    include(GenerateExportHeader)
    generate_export_header(${TARGET_NAME}
        BASE_NAME ${BASE_NAME_UPPER}
        EXPORT_MACRO_NAME ${EXPORT_MACRO_NAME}
        EXPORT_FILE_NAME ${EXPORT_HEADER_NAME}
    )

    # Ensure every module sees the <MODULE>_EXPORTS definition
    # that its manual export header expects (e.g. WINDOW_EXPORTS).
    target_compile_definitions(${TARGET_NAME} PRIVATE ${BASE_NAME_UPPER}_EXPORTS)

    # Ensure the binary directory is in the include path for this target
    # Add binary dir ONLY for build interface
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

    # Install the generated header along with public headers
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_HEADER_NAME}
        DESTINATION include/${PROJECT_NAME}
    )

    if(ARG_MODULES)
        set_source_files_properties(${ARG_MODULES} PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_HEADER_NAME}"
        )
    endif()
endfunction()

function(add_module)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs DEPENDS CONFIGS)
    cmake_parse_arguments(ARG "" "" "${multiValueArgs}" ${ARGN})

    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    )

    file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    )

    file(GLOB_RECURSE PRIVATE_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    )

    add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PRIVATE_HEADERS})

    target_sources(${PROJECT_NAME}
        PRIVATE
            ${SOURCES}
            ${PRIVATE_HEADERS}
        PUBLIC
            ${HEADERS}
    )

    target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${PROJECT_NAME} ${ARG_DEPENDS})

    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4251>
    )

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            $<$<CONFIG:Debug>:ENGINE_DEBUG>
            $<$<PLATFORM_ID:Darwin>:_LIBCPP_HAS_PARALLEL_ALGORITHMS=1>
    )

    create_export(${PROJECT_NAME})

    if(ENABLE_TESTS)
        enable_sanitizers()
    endif()

    target_configure_build(${PROJECT_NAME} ${ARG_CONFIGS})
    target_configure_platform(${PROJECT_NAME})
    target_configure_rpath(${PROJECT_NAME})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME ${PROJECT_NAME}
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endfunction()
