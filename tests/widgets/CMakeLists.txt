# issue #2656: "Insert As Relative Link" must preserve heading anchors. The
# anchor-append logic is extracted into the dependency-free LinkInsertUtils so
# it can be unit-tested without the full MarkdownEditor / clipboard / dialog
# stack. GUILESS — pure string helper, no widgets needed.
add_qt_test(test_linkinsertutils
  SOURCES
    test_linkinsertutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/linkinsertutils.cpp
  LINKS
    Qt6::Core
  GUILESS
)

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

add_qt_test(test_node_explorer_reload_expansion
  SOURCES
    test_node_explorer_reload_expansion.cpp
    ${CMAKE_SOURCE_DIR}/src/views/inodeexplorer.h
    ${CMAKE_SOURCE_DIR}/src/views/combinednodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/twocolumnsnodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmodeviewwrapper.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_node_explorer_reload_expansion PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_node_explorer_reload_expansion POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_node_explorer_reload_expansion>
)

add_qt_test(test_explorer_selection_after_save
  SOURCES
    test_explorer_selection_after_save.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/notebookpathhelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/views/inodeexplorer.h
    ${CMAKE_SOURCE_DIR}/src/views/combinednodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/twocolumnsnodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmodeviewwrapper.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)

target_include_directories(test_explorer_selection_after_save PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_explorer_selection_after_save POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_explorer_selection_after_save>
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
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/syncpage.h
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
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
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

add_qt_test(test_notebookexplorer2_fs_suppression
  SOURCES
    test_notebookexplorer2_fs_suppression.cpp
  LINKS
    core_services
    vxcore
)

# T11: NotebookExplorer2 multi-select smoke (currently QSKIP — see test source
# and .sisyphus/notepads/notebookexplorer2-multiselect-fix/issues.md for the
# construction blocker). Registered so the slot exists for future expansion.
# NOT GUILESS — QWidget needs QApplication.
add_qt_test(test_notebookexplorer2_multiselect
  SOURCES
    test_notebookexplorer2_multiselect.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

add_custom_command(TARGET test_notebookexplorer2_multiselect POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebookexplorer2_multiselect>
)

# T24 (open-notebook-remote-readonly): OpenNotebookDialog2 two-mode UI test.
# NOT GUILESS — needs QApplication for QDialog widget construction, QStackedWidget
# layout, and the QRadioButton click semantics. Compiles the dialog source plus
# its base classes inline because there is no separate widgets static library
# (opennotebookdialog2.cpp lives only inside the vnote executable target).
# Mirrors the source list of test_notebooksyncinfodialog2 with the addition of
# locationinputwithbrowsebutton.cpp (the local-mode root folder picker) and
# opennotebookcontroller.cpp (the controller the dialog instantiates).
add_qt_test(test_open_notebook_dialog2
  SOURCES
    test_open_notebook_dialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/opennotebookdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/locationinputwithbrowsebutton.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_open_notebook_dialog2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_open_notebook_dialog2 PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_open_notebook_dialog2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_open_notebook_dialog2>
)

# openurl-followups Item 2: OpenNotebookDialog2 mid-clone Cancel test.
# Same compile shape as test_open_notebook_dialog2 (dialog source + base
# classes + locationinputwithbrowsebutton + opennotebookcontroller). Drives
# the dialog through a real file:// bare-repo fixture and fires
# controller.cancelClone() to verify the dialog wires Cancel through to the
# cancellation token. Tolerant of race-lost iterations (file:// is fast).
# NOT GUILESS — needs QApplication for dialog construction + QSignalSpy.
add_qt_test(test_open_notebook_dialog2_cancel
  SOURCES
    test_open_notebook_dialog2_cancel.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/opennotebookdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/locationinputwithbrowsebutton.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_open_notebook_dialog2_cancel PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_open_notebook_dialog2_cancel PRIVATE VNOTE_TESTING)

# vxcore self-registers the git backend via a static-init token; the
# /WHOLEARCHIVE link option forces the registration TU into the test exe so
# the file:// clone path works. Mirrors the equivalent block in
# tests/controllers/CMakeLists.txt for test_open_notebook_controller_clone.
if(MSVC)
  target_link_options(test_open_notebook_dialog2_cancel PRIVATE /WHOLEARCHIVE:vxcore)
endif()
set_tests_properties(test_open_notebook_dialog2_cancel PROPERTIES
  TIMEOUT 180
)

add_custom_command(TARGET test_open_notebook_dialog2_cancel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_open_notebook_dialog2_cancel>
)

# T3: ViewWindow2::reload() (commit 94c53d36) + ViewSplit2 Reload menu (commit bf44c324).
# NOT GUILESS — needs QApplication for QMessageBox automation in dirty-buffer
# subtests (Save / Discard / Cancel branches).
#
# Scope adjustment: viewwindow2.cpp is NOT compiled inline because that would
# pull in ~12 transitive widget deps (attachmentpopup2, findandreplacewidget2,
# editreaddiscardaction, ...). Instead this test uses the stub-injection
# pattern from test_snippet_apply: viewwindow2.h provides the class shape
# (via AUTOMOC) and the test cpp defines no-op bodies for every ViewWindow2
# non-pure-virtual method to satisfy the linker. TestViewWindow2 then
# replicates the production reload() logic (see file header for rationale).
add_qt_test(test_viewsplit2_reload_menu
  SOURCES
    test_viewsplit2_reload_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/viewwindow2.h
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_viewsplit2_reload_menu PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_viewsplit2_reload_menu PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_viewsplit2_reload_menu POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_viewsplit2_reload_menu>
)

# T27: ViewWindow2 disables Save/Insert actions on read-only buffer.
# Same stub-injection pattern as test_viewsplit2_reload_menu — viewwindow2.cpp
# is not compiled inline to avoid pulling in ~12 widget transitive deps.
# NOT GUILESS — QAction belongs to QtGui, and the production runtime path
# materializes inside a real QFrame so we use QTEST_MAIN.
add_qt_test(test_view_window2_readonly_toolbar
  SOURCES
    test_view_window2_readonly_toolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/viewwindow2.h
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_view_window2_readonly_toolbar PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_view_window2_readonly_toolbar POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_view_window2_readonly_toolbar>
)

# T28: ViewWindow2 modal warning on read-only save attempts, with cooldown.
# Same stub-injection pattern as T27 / test_viewsplit2_reload_menu —
# viewwindow2.cpp is not compiled inline. The test enables the
# testSetReadOnlyWarningSuppressModal seam so QMessageBox::exec() does not
# block the event loop while still ticking the cooldown counter.
# NOT GUILESS — QMessageBox + QFrame require QApplication.
add_qt_test(test_view_window2_readonly_save_warning
  SOURCES
    test_view_window2_readonly_save_warning.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/viewwindow2.h
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_view_window2_readonly_save_warning PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_view_window2_readonly_save_warning POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_view_window2_readonly_save_warning>
)

# T26 (open-notebook-remote-readonly): Lock-icon badge for read-only
# notebooks in NotebookSelector2. Compiles the selector source + its
# transitive widget dependencies (combobox, navigationmode) inline because
# there is no separate widgets static library. NotebookSelector2 reads
# ConfigMgr2 / SessionConfig via ServiceLocator under nullptr-guards, so the
# test omits ConfigMgr2/ThemeService registration but still links
# core_configs to satisfy the dead-but-linked ConfigMgr2 symbol references.
# in transitively via WidgetUtils::safelyOpenUrl → MainWindow::tr) and a
# local ThemeService::paletteColor stub since the navigation label code
# path is dead in the read-only badge scenario but the linker still needs
# the symbol resolved.
# NOT GUILESS — needs QApplication for QComboBox widget construction.
add_qt_test(test_notebook_selector2_readonly_badge
  SOURCES
    test_notebook_selector2_readonly_badge.cpp
    test_themeservice_stub.cpp
    ${CMAKE_SOURCE_DIR}/src/data/core/core.qrc
    ${CMAKE_SOURCE_DIR}/src/widgets/notebookselector2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.h
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebook_selector2_readonly_badge PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_notebook_selector2_readonly_badge POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebook_selector2_readonly_badge>
)

# T4 (notebook-explorer-drag-reorder): SortDialog2 — pure-UI reorder dialog
# with Up/Down/Top/Bottom buttons and internal drag-drop. No ServiceLocator
# / vxcore dependency; the dialog accepts initial order and returns final
# order via getSortedOrder(). Sources: just the dialog .cpp + the test.
# LINKS: only Qt6::Widgets — the dialog uses pure standard Qt widgets, no
# WidgetsFactory / vxcore / VTextEdit transitive dependencies.
# NOT GUILESS — needs QApplication for QDialog widget construction.
add_qt_test(test_sort_dialog2
  SOURCES
    test_sort_dialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/sortdialog2.cpp
  LINKS
    Qt6::Widgets
)

# T11 (notebook-explorer-drag-reorder): wires SortDialog2 into the
# NotebookExplorer2 / CombinedNodeExplorer chain. Tests:
#   1. NotebookExplorer2::runSortDialogsForChildren (static testable seam used
#      by the onSortRequested slot). 7 sub-tests covering: two dialogs in
#      sequence (folders then files), accept folders + cancel files,
#      cancel both, accept unchanged (no-op), files-only, folders-only,
#      empty JSON skips both.
#   2. CombinedNodeExplorer::sortRequested forwarding from the inner
#      NotebookNodeController.
#   3. CombinedNodeExplorer::requestReorderNodes name-to-id translation.
# Compiles the static helper from notebookexplorer2.cpp inline (NOT the full
# slot — only the helper is referenced; the rest of notebookexplorer2.cpp
# has too many transitive widget deps to drag in here). The
# CombinedNodeExplorer chain reuses the test_explorer_selection_after_save
# source recipe (notebook node explorer family compiled inline).
# NOT GUILESS — QDialog::exec() needs QApplication.
add_qt_test(test_notebook_explorer2_sort
  SOURCES
    test_notebook_explorer2_sort.cpp
    ${CMAKE_SOURCE_DIR}/src/views/inodeexplorer.h
    ${CMAKE_SOURCE_DIR}/src/widgets/notebookexplorer2_sortseam.h
    ${CMAKE_SOURCE_DIR}/src/widgets/notebookexplorer2_sortseam.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/sortdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/views/combinednodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmodeviewwrapper.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebook_explorer2_sort PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_notebook_explorer2_sort POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebook_explorer2_sort>
)

# T12 (notebook-explorer-drag-reorder): TwoColumnsNodeExplorer sort wiring
# mirror of test_notebook_explorer2_sort, but for the two-pane explorer.
# Compiles TwoColumnsNodeExplorer + the notebook node explorer family inline
# (same recipe as test_explorer_selection_after_save). Tests:
#   T12a: NotebookNodeController::sortRequested forwarding from BOTH the
#         folder pane controller AND the file pane controller.
#   T12b: requestReorderNodes builds NodeIdentifier lists from flat name
#         lists (root-parent no-slash branch) and dispatches successfully.
#   T12c: Single-dispatch invariant — folder controller reorders, file
#         controller never sees the call.
# NOT GUILESS — QWidget needs QApplication.
add_qt_test(test_two_columns_node_explorer_sort
  SOURCES
    test_two_columns_node_explorer_sort.cpp
    ${CMAKE_SOURCE_DIR}/src/views/inodeexplorer.h
    ${CMAKE_SOURCE_DIR}/src/views/twocolumnsnodeexplorer.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmodeviewwrapper.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.h
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_two_columns_node_explorer_sort PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_two_columns_node_explorer_sort POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_two_columns_node_explorer_sort>
)

# T29 follow-up (open-notebook-remote-readonly): read-only banner visibility
# in NotebookSyncInfoDialog2. Mirrors the source layout of
# test_notebooksyncinfodialog2 — the dialog cpp + its base widgets are
# compiled inline because there is no separate widgets static library
# (notebooksyncinfodialog2.cpp lives only inside the vnote executable
# target). NOT GUILESS — needs QApplication for QDialog widget construction
# and QLabel visibility checks.
add_qt_test(test_notebook_sync_info_dialog2_readonly
  SOURCES
    test_notebook_sync_info_dialog2_readonly.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notebooksyncinfodialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_notebook_sync_info_dialog2_readonly PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_notebook_sync_info_dialog2_readonly PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_notebook_sync_info_dialog2_readonly POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebook_sync_info_dialog2_readonly>
)

# HistorySticker dashboard sticker test. NOT GUILESS — constructs a real
# QListView + FileNodeDelegate (needs QApplication). Compiles the sticker +
# its model/view/factory deps inline (there is no widgets static library) plus
# the ThemeService chain so FileNodeDelegate::paletteColor symbols link (the
# delegate paint path is never reached, so ThemeService is left unregistered).
# mainwindow.h is listed so AUTOMOC provides MainWindow::staticMetaObject used
# transitively by widgetutils.cpp's MainWindow::tr(...).
add_qt_test(test_historysticker
  SOURCES
    test_historysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/historysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/calendarsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/greetingsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/activitysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/sticker.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/stickerfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/models/historylistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_historysticker PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_historysticker POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_historysticker>
)

# ActivitySticker dashboard sticker test. NOT GUILESS — constructs real
# QWidgets (needs QApplication). Compiles the full built-in sticker source set
# because stickerfactory.cpp references every built-in sticker (calendar /
# history / greeting / activity) plus their transitive model/view/theme deps.
add_qt_test(test_activitysticker
  SOURCES
    test_activitysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/activitysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/historysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/calendarsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/greetingsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/sticker.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/stickerfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/models/historylistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_activitysticker PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_activitysticker POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_activitysticker>
)

# GUI test for GreetingSticker. NOT GUILESS (QLabel needs QApplication). Pulls in
# the full sticker source set because stickerfactory.cpp references every
# built-in sticker (calendar/history/greeting) and their transitive deps.
add_qt_test(test_greetingsticker
  SOURCES
    test_greetingsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/greetingsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/historysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/calendarsticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/activitysticker.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/sticker.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/stickerfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/models/historylistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    core_configs
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_greetingsticker PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_greetingsticker POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_greetingsticker>
)


# EncodingButton: reusable pure-view encoding picker (no services / no Buffer2).
# NOT GUILESS — QToolButton/QMenu need QApplication. Only needs Qt6::Widgets
# plus Qt6::Core5Compat (QTextCodec::availableCodecs); no VTextEdit runtime dep,
# so no DLL copy step is required.
add_qt_test(test_encoding_button
  SOURCES
    test_encoding_button.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/encodingbutton.cpp
  LINKS
    Qt6::Widgets
    Qt6::Core5Compat
)
