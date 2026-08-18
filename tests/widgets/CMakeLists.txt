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

# ImageLinkLookup: "is there an image under the cursor?" for the editor's Image
# context submenu. Extracted from MarkdownEditor so it can be tested without the
# editor, the highlighter or a document: it takes plain ints rather than a
# QTextBlock, following the LinkInsertUtils precedent. GUILESS — no widgets,
# only vte::md::ImageLinkInfo, which is a header-only struct.
add_qt_test(test_imagelinklookup
  SOURCES
    test_imagelinklookup.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/imagelinklookup.cpp
  LINKS
    Qt6::Core
    VTextEdit
  GUILESS
)
target_include_directories(test_imagelinklookup PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

# The span arithmetic behind `Image > Set Size...`. Extracted from
# MarkdownEditor so it can be gated directly: every defect in it corrupts the
# tag rather than merely producing a wrong size.
add_qt_test(test_imagesizeedits
  SOURCES
    test_imagesizeedits.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/imagesizeedits.cpp
  LINKS
    Qt6::Core
    VTextEdit
  GUILESS
)
target_include_directories(test_imagesizeedits PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

add_custom_command(TARGET test_imagelinklookup POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_imagelinklookup>
)

# InlineBanner: reusable pure-view notification strip (no services, no vxcore,
# no VTextEdit). NOT GUILESS — QFrame/QPushButton need a QApplication. Compiles
# propertydefs.cpp for the BannerSeverity constant and widgetutils.cpp for the
# unpolish/polish helper; widgetutils.cpp includes <vtextedit/vtextedit.h>, so
# VTextEdit is linked and the DLL copy below is required.
add_qt_test(test_inlinebanner
  SOURCES
    test_inlinebanner.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logging.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
)
target_include_directories(test_inlinebanner PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_inlinebanner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_inlinebanner>
)

# LegacyImageMigrationBar: the View wiring of the legacy-image prompt (three
# buttons -> three intents). The migration policy lives in
# tests/controllers/test_legacy_image_migration.cpp; this only pins what the
# InlineBanner extraction rewrote. Same source recipe as test_inlinebanner.
add_qt_test(test_legacyimagemigrationbar
  SOURCES
    test_legacyimagemigrationbar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/legacyimagemigrationbar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logging.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
)
target_include_directories(test_legacyimagemigrationbar PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_legacyimagemigrationbar POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_legacyimagemigrationbar>
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
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
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

# NotebookExplorer2 session-state wire format PLUS MainWindow2's
# PendingCaptureDispatcher (the FIFO / readiness / reentrancy state machine
# behind the macOS "Create Note in VNote" Service). Neither needs the widgets
# themselves: the state format is asserted against the raw QDataStream bytes and
# the dispatcher is defined inline in mainwindow2.h, so mainwindow2.cpp (and its
# whole transitive widget graph) is deliberately NOT compiled here.
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
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_shareseam.cpp
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
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_shareseam.cpp
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

# Persistence regressions on two REAL settings pages (test_settings_slug above
# stubs their method bodies, so it cannot cover load/save behavior). The pages
# are constructed for real, hence QTEST_MAIN and no GUILESS. NewQuickAccessItemDialog
# and its Dialog/ScrollDialog bases are compiled in because QuickAccessPage
# references the dialog from its "New" handler; stubbing it would need the same
# bases anyway (moc + virtuals), so the real sources are cheaper.
add_qt_test(test_settings_persistence
  SOURCES
    test_settings_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/quickaccesspage.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/fileassociationpage.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/settingspage.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/settingspagehelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/settings/newquickaccessitemdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notetemplateselector.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/locationinputwithbrowsebutton.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
  LINKS
    core_configs
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

target_include_directories(test_settings_persistence PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

# vxcore test mode redirects every process to the SAME config directory, and
# this test writes the session config, so it must not run beside another test
# that does the same.
set_tests_properties(test_settings_persistence PROPERTIES RUN_SERIAL TRUE)

add_custom_command(TARGET test_settings_persistence POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_settings_persistence>
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
# NOT GUILESS — visualBlockedDialog drives a real ManageNotebooksDialog2 and
# reads back its information banner, which is now the user-facing channel for
# the sync close-block reason (the old QMessageBox popped from inside
# core_services is gone). The dialog + its base widgets + the controller are
# compiled in because there is no separate widgets static library.
# Per the T6/T7/T11 deviation: link via core_services rather than dual-compiling
# syncservice/syncworker/synccredentialsstore (would cause duplicate-symbol
# errors and break the VNOTE_KEYCHAIN_AVAILABLE compile gate).
# VNOTE_TESTING is reserved on the test target per ADR-6.
add_qt_test(test_syncservice_lifecycle
  SOURCES
    test_syncservice_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/managenotebooksdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/listwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/styleditemdelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/simplesegmenthighlighter.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/navigationmode.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
    ${CMAKE_SOURCE_DIR}/src/controllers/managenotebookscontroller.cpp
  LINKS
    core_services
    core_configs
    vxcore
    # src/gui/utils/widgetutils.cpp includes <vtextedit/vtextedit.h>.
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_syncservice_lifecycle PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
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
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
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
  GUILESS
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

# ImportFolderDialog2 two-mode UI test (import-shared-folder-bundle).
# NOT GUILESS — needs QApplication for QDialog widget construction,
# QStackedWidget layout and QRadioButton click semantics. Compiles the dialog
# source plus its base classes inline for the same reason as
# test_open_notebook_dialog2: there is no widgets static library, those sources
# live only inside the vnote executable target. selectionitemwidget.cpp is on
# the list because FolderFilesFilterWidget constructs one.
add_qt_test(test_import_folder_dialog2
  SOURCES
    test_import_folder_dialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/importfolderdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/folderfilesfilterwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/scrolldialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/locationinputwithbrowsebutton.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/selectionitemwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    # widgetsfactory.cpp references both of these, so they must be compiled in
    # too (same reason test_open_notebook_dialog2 lists them).
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/importfoldercontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)
target_include_directories(test_import_folder_dialog2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
target_compile_definitions(test_import_folder_dialog2 PRIVATE VNOTE_TESTING)

add_custom_command(TARGET test_import_folder_dialog2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_import_folder_dialog2>
)

# T24 (open-notebook-remote-readonly): OpenNotebookDialog2 two-mode UI test.
# NOT GUILESS — needs QApplication for QDialog widget construction, QStackedWidget
# layout, and the QRadioButton click semantics. Compiles the dialog source plus
# its base classes inline because there is no separate widgets static library
# (opennotebookdialog2.cpp lives only inside the vnote executable target).
# Mirrors the source list of test_notebooksyncinfodialog2 with the addition of
# locationinputwithbrowsebutton.cpp (the local-mode root folder picker) and
# opennotebookcontroller.cpp (the controller the dialog instantiates).
#
# Also hosts the NewNoteDialog2 constructor-options coverage (macOS Services
# note capture), which reuses this harness's fully wired ServiceLocator: hence
# newnotedialog2.cpp + notetemplateselector.cpp + newnotecontroller.cpp, and
# core_configs for ConfigMgr2/WidgetConfig. fileutils2.cpp is no longer compiled
# inline because core_configs already provides it.
add_qt_test(test_open_notebook_dialog2
  SOURCES
    test_open_notebook_dialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/opennotebookdialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/newnotedialog2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/notetemplateselector.cpp
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
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
    core_services
    core_configs
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
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_shareseam.cpp
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
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filenodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_shareseam.cpp
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
    ${CMAKE_SOURCE_DIR}/src/widgets/inlinebanner.cpp
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

# StatusBar: reusable column-based status bar infra (no services / no Buffer2).
# NOT GUILESS — QLabel/QToolButton/QLineEdit/QMenu need QApplication. Only needs
# Qt6::Widgets; no VTextEdit runtime dep, so no DLL copy step is required.
add_qt_test(test_statusbar
  SOURCES
    test_statusbar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/statusbar.cpp
  LINKS
    Qt6::Widgets
)

# issue #2722: TitleBarContainer must survive QMainWindow::menuBar() (KDE Breeze
# calls it on every main window it polishes) without inheriting QMenuBar's
# action-derived sizing, Alt-key navigation or accessibility role. Pure-view
# container with no services, so only Qt6::Widgets is needed; no VTextEdit
# runtime dep, so no DLL copy step is required. NOT GUILESS — real QMainWindow /
# QMenuBar geometry needs a QApplication.
add_qt_test(test_titlebarcontainer
  SOURCES
    test_titlebarcontainer.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/titlebarcontainer.cpp
  LINKS
    Qt6::Widgets
)

# EditorStatusBarBinder: wires a real vte::VTextEditor to a StatusBar. NOT
# GUILESS (needs QApplication); links VTextEdit (loaded at runtime -> DLL copy).
add_qt_test(test_editorstatusbarbinder
  SOURCES
    test_editorstatusbarbinder.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/statusbar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/editorstatusbarbinder.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
)

add_custom_command(TARGET test_editorstatusbarbinder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_editorstatusbarbinder>
)

# MarkdownViewerAdapter heading-anchor plumbing: the C++ request/response
# bridge for the edit-mode "Copy Link" heading action. No web view is created;
# the web side is simulated by listening for headingAnchorRequested and calling
# the setHeadingAnchor slot. NOT GUILESS — MarkdownViewerAdapter pulls in
# GuiUtils/QTextCharFormat, which need a QApplication.
add_qt_test(test_markdownvieweradapter_anchor
  SOURCES
    test_markdownvieweradapter_anchor.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/markdownvieweradapter.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/webviewadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/plantumlhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/graphvizhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/graphhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/outlineprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/guiutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/callbackpool.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
  LINKS
    Qt6::Widgets
    # graphhelper.h includes <vtextedit/lrucache.h>. This used to arrive
    # transitively through core_services; that link was severed.
    VTextEdit
    core_services
    vxcore
)

# NotificationToast - the transient surface for Attention::Interrupt messages.
# NOT GUILESS: constructs a real QFrame with child buttons and drives mouse
# events, so it needs a QApplication. The attention->surface routing lives
# INSIDE the toast (with the window policy arriving through injected seams),
# which is what lets this assert production behaviour rather than a copy of it.
add_qt_test(test_notificationtoast
  SOURCES
    test_notificationtoast.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/notificationtoast.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/widgetutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/messageboxhelper.cpp
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
target_include_directories(test_notificationtoast PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

# NotificationPopup2 + NotificationButton2 - the persistent notification centre.
# NOT GUILESS: builds real QMenu/QFrame/QPushButton widgets. Covers the
# retention path in particular: a message can leave the store WITHOUT being
# dismissed (renotify replacement, or cap eviction), and both the popup rows and
# the badge must honour messageRemoved.
add_qt_test(test_notificationpopup2
  SOURCES
    test_notificationpopup2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/notificationpopup2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/notificationbutton2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/buttonpopup.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/titlebar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/widgetsfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineedit.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/lineeditwithsnippet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/combobox.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/propertydefs.cpp
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
target_include_directories(test_notificationpopup2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)
# PDF outline transport: PdfViewerAdapter's JSON -> Heading conversion, the
# makePerfectHeadings pass (which INSERTS filler headings and so shifts
# positions), and the index-based jump request. GUILESS: PdfViewerAdapter and
# its base pull in no widgets, but webviewadapter.cpp calls Utils::sleepWait,
# which arrives with core_services.
add_qt_test(test_pdfvieweradapter_outline
  SOURCES
    test_pdfvieweradapter_outline.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/pdfvieweradapter.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/webviewadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/outlineprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/callbackpool.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

# JS half of the PDF outline contract, evaluated with QJSEngine against the REAL
# src/data/extra/web/pdf.js/{pdfviewercore.js,pdfviewer.js}. This is the only
# gate covering the two bugs the outline design exists to prevent (a lossy
# {name, level} wire format, and an adapter-presence guard that discards the
# outline when the PDF loads before the QWebChannel callback runs).
#
# Guarded on Qt6::Qml because QJSEngine lives there. tests/CMakeLists.txt
# requests Qml as an OPTIONAL_COMPONENT; without qtdeclarative installed this
# target is silently skipped, so CI asserts the test is REGISTERED (see the
# ci-*.yml "Verify test registration" steps) rather than trusting a green run.
if(TARGET Qt6::Qml)
  add_qt_test(test_pdfviewercore_js
    SOURCES
      test_pdfviewercore_js.cpp
    LINKS
      # Guarding on the target does NOT link QJSEngine; this does.
      Qt6::Qml
    GUILESS
  )
  target_compile_definitions(test_pdfviewercore_js PRIVATE
    VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
  )

  # TurndownConverter's image rule. It was defined but never installed for its
  # entire existence, which silently dropped every declared image size on
  # "Parse to Markdown and Paste" -- a defect no C++ test could observe.
  # Evaluates the REAL src/data/extra/web/js/turndown.js and stubs only its
  # collaborators, so the rule under test is the production one.
  add_qt_test(test_turndown_js
    SOURCES
      test_turndown_js.cpp
    LINKS
      # Guarding on the target does NOT link QJSEngine; this does.
      Qt6::Qml
      # Expectations are built by the PRODUCTION generator rather than spelled
      # as literals, so a C++-side change to the canonical tag fails this gate.
      VTextEdit
    GUILESS
  )
  target_compile_definitions(test_turndown_js PRIVATE
    VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
  )
endif()

# In-place preview zoom: the pure scale arithmetic (zoom ratio vs. C++ raster factor).
# Also covers GraphPreviewData, which owns the rasterization of an in-place
# preview. NOT GUILESS - QPixmap/QSvgRenderer need a QGuiApplication.
add_qt_test(test_previewscaleutils
  SOURCES
    test_previewscaleutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/previewscaleutils.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/editors/graphpreviewdata.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/imageutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Svg
)

# Pure sticker drag math (hit zones + raw candidate geometry). GUILESS: only
# QSize/QPoint value types are involved. Compiles ONLY stickerdraggeometry.cpp
# -- the built-in sticker sources are deliberately not pulled in (stickerfactory
# references every one of them, which is what makes the sticker tests heavy).

add_qt_test(test_stickerdraggeometry
  SOURCES
    test_stickerdraggeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/stickerdraggeometry.cpp
  GUILESS
)

# StickerDragOverlay: the per-sticker edit-mode overlay. NOT GUILESS -- it is a
# real QWidget driven with synthetic mouse/key events, so it needs QApplication.
# Only the overlay + the pure math are compiled; the overlay takes no
# ServiceLocator and knows nothing about the board.
add_qt_test(test_stickerdragoverlay
  SOURCES
    test_stickerdragoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/stickerdragoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/stickerdraggeometry.cpp
)
