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
FetchContent_MakeAvailable(frozenStarCrystal-core)

# ==============================================================================
# Include Subdirectory Configurations
# ==============================================================================
add_subdirectory(graphics)
add_subdirectory(math)
add_subdirectory(threading)
add_subdirectory(utility)

message(STATUS "Dependencies configured successfully")
