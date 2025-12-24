cmake_minimum_required(VERSION 3.13)

project(react-native-io LANGUAGES CXX)

# ==============================================================================
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Subdirectories
# ==============================================================================
add_subdirectory(hash)
add_subdirectory(network)

# ==============================================================================
# Main Library
# ==============================================================================
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME} SHARED
  ${LIB_SOURCES}
  $<TARGET_OBJECTS:rct_network>
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hash>
    $<TARGET_PROPERTY:rct_network,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(${PROJECT_NAME}
  io_hash
  react_codegen_StdIOSpec
  jsi
  reactnative
  fbjni
  log
)

target_compile_options(${PROJECT_NAME}
  PRIVATE
    -fexceptions
    -frtti
    -Wall
    -Wextra
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
)
