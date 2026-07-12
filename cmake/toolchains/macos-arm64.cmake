# ==============================================================================
# Toolchain file for cross-compiling to macOS arm64 from Linux using osxcross
# ==============================================================================
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)

# osxcross compilers (set OSXCROSS_ROOT before using this toolchain)
if(NOT DEFINED OSXCROSS_ROOT)
    set(OSXCROSS_ROOT "/opt/osxcross" CACHE PATH "Path to osxcross installation")
endif()

set(CMAKE_C_COMPILER "${OSXCROSS_ROOT}/bin/oa64-clang")
set(CMAKE_CXX_COMPILER "${OSXCROSS_ROOT}/bin/oa64-clang++")

# Architecture flags
add_compile_options(-arch arm64)
add_link_options(-arch arm64)

# Search paths
set(CMAKE_FIND_ROOT_PATH "${OSXCROSS_ROOT}/SDK")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
