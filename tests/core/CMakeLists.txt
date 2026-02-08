# Core module tests
add_qt_test(test_error
  SOURCES
    test_error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  GUILESS
)

add_qt_test(test_exception
  SOURCES
    test_exception.cpp
  GUILESS
)
