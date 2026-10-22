cmake_minimum_required(VERSION 3.22)
project(RNOHIo LANGUAGES CXX)

if(NOT TARGET rnoh)
  message(FATAL_ERROR "Create the host rnoh target before calling autolink_libraries")
endif()

# Remote Communication Kit is supplied by Huawei's SDK, outside the OpenHarmony
# sysroot. Resolve its target-architecture stub rather than assuming that the
# host has added a global -L path. Custom SDK layouts can override these caches.
get_filename_component(IO_HMS_NATIVE_DEFAULT
  "${OHOS_SDK_NATIVE}/../../hms/native" ABSOLUTE)
set(IO_HMS_NATIVE "${IO_HMS_NATIVE_DEFAULT}" CACHE PATH "HarmonyOS HMS native SDK")
find_path(IO_RCP_INCLUDE_DIR RemoteCommunicationKit/rcp.h
  PATHS "${IO_HMS_NATIVE}/sysroot/usr/include"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(IO_RCP_LIBRARY NAMES rcp_c
  PATHS "${IO_HMS_NATIVE}/sysroot/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)

set(IO_CORE "${CMAKE_CURRENT_SOURCE_DIR}/io-core")
# The source tree is synchronized by the HAR builder; the host owns RN and Hermes.
add_subdirectory("${IO_CORE}/hash" io_hash)
add_library(rnoh_io STATIC
  "${IO_CORE}/src/NativeStdIO.cpp"
  HarmonyLogger.cpp
  IOHttpClientHarmony.cpp
)
set_target_properties(rnoh_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(rnoh_io PUBLIC cxx_std_20)
target_compile_definitions(rnoh_io PRIVATE RCT_IO_HARMONY=1)
target_include_directories(rnoh_io PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${IO_CORE}/src"
  "${IO_CORE}/hash"
  "${IO_CORE}/network"
)
target_include_directories(rnoh_io SYSTEM PRIVATE "${IO_RCP_INCLUDE_DIR}")
target_link_libraries(rnoh_io PUBLIC rnoh io_hash
  libhilog_ndk.z.so libability_runtime.so "${IO_RCP_LIBRARY}")
