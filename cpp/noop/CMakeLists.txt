cmake_minimum_required(VERSION 3.13)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB react_codegen_SRCS CONFIGURE_DEPENDS *.cpp)

add_library(react_codegen_StdIOSpec
  STATIC
  ${react_codegen_SRCS}
)

target_include_directories(react_codegen_StdIOSpec PUBLIC .)

target_link_libraries(react_codegen_StdIOSpec
  fbjni
  jsi
  reactnative
)
