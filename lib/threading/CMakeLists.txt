# ==============================================================================
# Threading Dependencies
# ==============================================================================

find_package(TBB QUIET)   # NOT REQUIRED

if(TBB_FOUND)
    message(STATUS "Using system TBB at ${TBB_DIR}")
else()
    CPMAddPackage(
        NAME TBB
        GITHUB_REPOSITORY uxlfoundation/onetbb
        GIT_TAG v2023.2.0-rc1
        GIT_SHALLOW TRUE
        OPTIONS
            "TBB_TEST OFF"
            "TBB_EXAMPLES OFF"
    )
endif()

# Create an interface library to abstract the actual TBB target name
add_library(tbb_engine INTERFACE)



if(TARGET TBB::tbb)
    target_link_libraries(tbb_engine INTERFACE TBB::tbb)
elseif(TARGET tbb)
    target_link_libraries(tbb_engine INTERFACE tbb)
else()
    # Fallback for older FindTBB.cmake that sets TBB_LIBRARIES
    target_link_libraries(tbb_engine INTERFACE ${TBB_LIBRARIES})
    target_include_directories(tbb_engine INTERFACE ${TBB_INCLUDE_DIRS})
endif()

message(STATUS "Threading dependencies configured")
