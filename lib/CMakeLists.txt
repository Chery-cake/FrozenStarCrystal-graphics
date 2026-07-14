# ==============================================================================
# Dependencies - Main Configuration
# ==============================================================================
# Manages all external dependencies using FetchContent where feasible.
# Organized by subsystem: graphics, audio, physics, math, threading, ecs, etc.
# ==============================================================================

include(FetchContent)

# Set FetchContent options
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
  frozenStarCrystal-core
  GIT_REPOSITORY https://github.com/Chery-cake/FrozenStarCrystal-core.git
  GIT_TAG master
  GIT_SHALLOW TRUE
  GIT_PROGRESS true
)
set(_saved_cflags  "${CMAKE_C_FLAGS}")
set(_saved_cxxflags  "${CMAKE_CXX_FLAGS}")
set(_saved_exeflags  "${CMAKE_EXE_LINKER_FLAGS}")
set(_saved_sharedflags  "${CMAKE_SHARED_LINKER_FLAGS}")
set(_saved_modflags  "${CMAKE_MODULE_LINKER_FLAGS}")
set(ENABLE_TESTS OFF)
FetchContent_MakeAvailable(frozenStarCrystal-core)
set(CMAKE_C_FLAGS  "${_saved_cflags}")
set(CMAKE_CXX_FLAGS  "${_saved_cxxflags}")
set(CMAKE_EXE_LINKER_FLAGS  "${_saved_exeflags}")
set(CMAKE_SHARED_LINKER_FLAGS  "${_saved_sharedflags}")
set(CMAKE_MODULE_LINKER_FLAGS  "${_saved_modflags}")

# ==============================================================================
# Include Subdirectory Configurations
# ==============================================================================
add_subdirectory(graphics)
#add_subdirectory(math)
add_subdirectory(threading)
#add_subdirectory(utility)

message(STATUS "Dependencies configured successfully")
