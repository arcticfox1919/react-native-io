# ==============================================================================
# IO Network Module (Android only)
# ==============================================================================

if(ANDROID)
  add_library(rct_network OBJECT
    IOHttpClientAndroid.cpp
  )

  target_include_directories(rct_network
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  )

  target_compile_features(rct_network PUBLIC cxx_std_20)

  target_link_libraries(rct_network
    PUBLIC
      fbjni
      reactnative
      log
  )

  set_target_properties(rct_network PROPERTIES
    POSITION_INDEPENDENT_CODE ON
  )
endif()
