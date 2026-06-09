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

add_qt_test(test_scrollpreservationpolicy
  SOURCES
    test_scrollpreservationpolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/scrollpreservationpolicy.cpp
  GUILESS
)

add_qt_test(test_headingslug
  SOURCES
    test_headingslug.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/headingslugger.cpp
  GUILESS
)

add_qt_test(test_stringutils
  SOURCES
    test_stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  GUILESS
)

add_qt_test(test_clipboard_image
  SOURCES
    test_clipboard_image.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/clipboardutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/imageresolutionutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/global.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    VTextEdit
  GUILESS
)

add_qt_test(test_urlutils
  SOURCES
    test_urlutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/urlutils.cpp
  GUILESS
)

add_qt_test(test_fileutils2_staging
  SOURCES
    test_fileutils2_staging.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

# Grep-gate regression test: prevents reintroduction of shared-schema JSON
# key literals in src/. See tests/utils/test_json_key_drift.cpp for the full
# contract (gated literal set, allow-list, escape hatch).
add_qt_test(test_json_key_drift
  SOURCES
    test_json_key_drift.cpp
  GUILESS
)
target_compile_definitions(test_json_key_drift PRIVATE
  VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

target_include_directories(test_clipboard_image PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/include
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_clipboard_image POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_clipboard_image>
)
