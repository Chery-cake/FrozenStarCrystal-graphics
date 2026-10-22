# ==============================================================================
# Dependencies - Main Configuration
# ==============================================================================
# All third-party deps flow through CPM (loaded by fsc_initialize_project).
# ==============================================================================

CPMUsePackageLock("${CMAKE_CURRENT_SOURCE_DIR}/cmake/package-lock.cmake")

# --- FSC core ----------------------------------------------------------------
# Prefers a local checkout registered via cmake/local-paths.cmake.
# Falls back to a fresh CPM download into build/_deps/.
fsc_cpm_or_local(FrozenStarCrystal-core
    GITHUB_REPOSITORY Chery-cake/FrozenStarCrystal-core
    GIT_TAG master
    DOWNLOAD_AND_ADD_SUBDIRECTORY
    OPTIONS
        "ENABLE_TESTS OFF"
        "FSC_TESTS OFF"
)

if(NOT TARGET FrozenStarCrystal-core)
    message(FATAL_ERROR
        "FrozenStarCrystal-core's CMakeLists.txt did not create a target "
        "named 'FrozenStarCrystal-core'. Most commonly this means "
        "fsc_initialize_project() aborted early — check for a stray "
        "return() inside a macro() in FSCProject.cmake.")
endif()

# ==============================================================================
# Subsystem dependencies
# ==============================================================================
add_subdirectory(graphics)

message(STATUS "Dependencies configured successfully")
