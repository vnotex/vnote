add_qt_test(test_navigationmodeservice
  SOURCES
    test_navigationmodeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/services/configcoreservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/navigationmodeservice.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
    vxcore
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
