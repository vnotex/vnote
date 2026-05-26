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

# T17: SyncService lifecycle (NotebookBeforeClose hook + bounded shutdown).
# NOT GUILESS — needs QApplication for the QMessageBox popped from the
# NotebookBeforeClose hook handler. Per the T6/T7/T11 deviation: link via
# core_services rather than dual-compiling syncservice/syncworker/synccredentialsstore
# (would cause duplicate-symbol errors and break the VNOTE_KEYCHAIN_AVAILABLE
# compile gate). VNOTE_TESTING is reserved on the test target per ADR-6.
add_qt_test(test_syncservice_lifecycle
  SOURCES
    test_syncservice_lifecycle.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_compile_definitions(test_syncservice_lifecycle PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_syncservice_lifecycle POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_syncservice_lifecycle>
)

# W3.T2: NotebookSyncInfoDialog2 routing tests (Bug G dialog fix).
# NOT GUILESS — needs QApplication for QDialog widget construction. Compiles
# the full dialog source + its base classes + WidgetsFactory + WidgetUtils
# chain explicitly because there is no separate widgets static library
# (notebooksyncinfodialog2.cpp lives only inside the vnote executable target).
# Adds widgets/mainwindow.h to SOURCES so AUTOMOC generates moc_mainwindow.cpp
# (which provides MainWindow::staticMetaObject used transitively by
# widgetutils.cpp's MainWindow::tr(...) call). We do NOT compile
# mainwindow.cpp; only the moc-generated symbols are needed for that one tr.
add_qt_test(test_notebooksyncinfodialog2
  SOURCES
    test_notebooksyncinfodialog2.cpp
    test_mainwindow_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/mainwindow.h
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebooksyncinfodialog2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_notebooksyncinfodialog2 PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_notebooksyncinfodialog2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooksyncinfodialog2>
)

# Tasks 2.1 (B1) + 2.2 (B2): wire confirmUrlChangeRequested + error signals
# from NotebookSyncInfoController to NotebookSyncInfoDialog2. Same source
# layout as test_notebooksyncinfodialog2 (the full dialog source + base
# widgets compiled inline because there is no separate widgets static lib).
add_qt_test(test_notebooksyncinfodialog2_url_change
  SOURCES
    test_notebooksyncinfodialog2_url_change.cpp
    test_mainwindow_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/mainwindow.h
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebooksyncinfodialog2_url_change PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_notebooksyncinfodialog2_url_change PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_notebooksyncinfodialog2_url_change POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooksyncinfodialog2_url_change>
)

add_qt_test(test_notebooksyncinfodialog2_error
  SOURCES
    test_notebooksyncinfodialog2_error.cpp
    test_mainwindow_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/mainwindow.h
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebooksyncinfodialog2_error PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_notebooksyncinfodialog2_error PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_notebooksyncinfodialog2_error POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooksyncinfodialog2_error>
)

# T1: NotebookSyncInfoDialog2 Disable closes dialog.
# NOT GUILESS — needs QApplication for QDialog widget construction and
# QMessageBox confirmation. Same source layout as other notebooksyncinfodialog2
# tests (full dialog source + base widgets compiled inline).
add_qt_test(test_notebooksyncinfodialog2_disable
  SOURCES
    test_notebooksyncinfodialog2_disable.cpp
    test_mainwindow_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/mainwindow.h
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebooksyncinfodialog2_disable PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_notebooksyncinfodialog2_disable PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_notebooksyncinfodialog2_disable POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooksyncinfodialog2_disable>
)

# W4.T1: NotebookExplorer2 sync state classifier tests (S2/S4 partial detection).
# Tests the extended partialSyncConfig classifier that now detects:
#   - S2: syncEnabled && syncReady but PAT missing
#   - S4: syncEnabled && syncReady but sync not registered
#   - S5: fully ready state
add_qt_test(test_notebook_explorer_sync_state
  SOURCES
    test_notebook_explorer_sync_state.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

add_custom_command(TARGET test_notebook_explorer_sync_state POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebook_explorer_sync_state>
)
