# Test helpers - header-only interface library
add_library(test_helpers INTERFACE)

target_include_directories(test_helpers INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
