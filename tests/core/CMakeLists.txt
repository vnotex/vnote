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

add_qt_test(test_servicelocator
  SOURCES
    test_servicelocator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/servicelocator.cpp
  GUILESS
)

add_qt_test(test_searchservice
  SOURCES
    test_searchservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_configservice
  SOURCES
    test_configservice.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_notebookservice
  SOURCES
    test_notebookservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
