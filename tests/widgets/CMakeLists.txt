add_qt_test(test_snippet_apply
  SOURCES
    test_snippet_apply.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/viewwindow2.h
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_snippet_apply POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_snippet_apply>
)
