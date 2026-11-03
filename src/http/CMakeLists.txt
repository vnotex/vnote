include(FetchContent)
find_package(Threads REQUIRED)

# Fetch only: do not run upstream's CMake (which probes/enables optional TLS and
# compression libraries). Keep all dependency settings private to this target.
FetchContent_Declare(vnote_cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG 24e58ce5b25d55045af4cd6f463e83a8082fbe6b # v0.64.0, MIT
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR vnote-fetch-only
)
FetchContent_MakeAvailable(vnote_cpp_httplib)

add_library(vnote_http STATIC httpserver.cpp httpserver.h httptypes.h)
target_compile_features(vnote_http PUBLIC cxx_std_17)
target_link_libraries(vnote_http PUBLIC Qt6::Core PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(vnote_http PRIVATE ws2_32)
endif()
target_include_directories(vnote_http
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
  PRIVATE ${vnote_cpp_httplib_SOURCE_DIR}
)
target_compile_definitions(vnote_http PRIVATE
  CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=4096
  CPPHTTPLIB_HEADER_MAX_LENGTH=16384
  CPPHTTPLIB_MAX_LINE_LENGTH=16384
  CPPHTTPLIB_HEADER_MAX_COUNT=100
)
set_target_properties(vnote_http PROPERTIES
  AUTOMOC OFF
  VNOTE_HTTP_LICENSE_FILE "${vnote_cpp_httplib_SOURCE_DIR}/LICENSE"
)
