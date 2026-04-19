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

add_qt_test(test_notebooknodeview_state
  SOURCES
    test_notebooknodeview_state.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.h
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

add_qt_test(test_notebookexplorer2_state
  SOURCES
    test_notebookexplorer2_state.cpp
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

# Copy VTextEdit DLL next to the new test executables so CTest can find it at runtime.
add_custom_command(TARGET test_notebooknodeview_state POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooknodeview_state>
)

add_custom_command(TARGET test_notebookexplorer2_state POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebookexplorer2_state>
)

add_qt_test(test_settings_slug
  SOURCES
    test_settings_slug.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/settingspage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/generalpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/notemanagementpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/appearancepage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/themepage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/quickaccesspage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/editorpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/imagehostpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/vipage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/texteditorpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/markdowneditorpage.h
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/fileassociationpage.h
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

target_include_directories(test_settings_slug PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_settings_slug POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_settings_slug>
)

add_qt_test(test_settings_navigation
  SOURCES
    test_settings_navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/settingspage.h
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

target_include_directories(test_settings_navigation PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_settings_navigation POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_settings_navigation>
)
