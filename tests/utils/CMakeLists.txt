# Utils module tests
add_qt_test(test_pathutils
  SOURCES
    test_pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

add_qt_test(test_htmlutils
  SOURCES
    test_htmlutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/htmlutils.cpp
  GUILESS
)

add_qt_test(test_fileutils2
  SOURCES
    test_fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Gui
  GUILESS
)
