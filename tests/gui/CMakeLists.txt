add_qt_test(test_navigationmodeservice
  SOURCES
    test_navigationmodeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/coreconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/navigationmodeservice.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
)

target_include_directories(test_navigationmodeservice PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/include
  ${CMAKE_SOURCE_DIR}/src/gui
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_navigationmodeservice POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_navigationmodeservice>
)

add_qt_test(test_themeservice
  SOURCES
    test_themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    core_services
)
