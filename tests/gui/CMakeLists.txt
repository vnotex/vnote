add_qt_test(test_navigationmodeservice
  SOURCES
    test_navigationmodeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/coreconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/navigationmodeservice.cpp
  LINKS
    Qt6::Widgets
    VTextEdit
    core_services
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

# Real-editor coverage of the reload caret-preservation helpers. Needs a real
# QApplication + an exposed widget so soft wrapping and the scrollbar range are
# genuine, hence tests/gui (NOT GUILESS).
add_qt_test(test_textcursorpreserver
  SOURCES
    test_textcursorpreserver.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/textcursorpreserver.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/cursorpreservationpolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/scrollpreservationpolicy.cpp
  LINKS
    Qt6::Widgets
)
target_include_directories(test_textcursorpreserver PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

add_qt_test(test_themeservice
  SOURCES
    test_themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/commentcolorswatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    core_services
)

# The comment colour chip, shared by all three colour pickers. It is a LEAF: it
# names no ThemeService symbol, so it links on its own. That property is what
# lets test_commentpanel and test_pdfannotationtoolbar compile the widgets that
# draw swatches without linking themeservice.cpp.
add_qt_test(test_commentcolorswatch
  SOURCES
    test_commentcolorswatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/commentcolorswatch.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    core_services
)

# T10 (notebook-explorer-drag-reorder): FileListView drag-reorder routing.
# Compiles filelistview.cpp + notebooknodeproxymodel.cpp + (transitively) the
# minimal slice of utils needed by the proxy. notebooknodecontroller is
# stubbed inline in the test TU so the linker never reaches the production
# controller object — saves us from dragging in the whole UI graph.
add_qt_test(test_file_list_view_reorder
  SOURCES
    test_file_list_view_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/views/filelistview.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.h
  LINKS
    Qt6::Widgets
    core_services
    vxcore
    VTextEdit
)

# Copy VTextEdit DLL next to the new test executable so CTest can find it.
add_custom_command(TARGET test_file_list_view_reorder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_file_list_view_reorder>
)

# T9 (notebook-explorer-drag-reorder): NotebookNodeView drag-drop dispatch.
# Lives in tests/gui (NOT GUILESS) because it needs a real QApplication and
# widget exposure for visualRect()/indexAt() to return real geometry. Uses the
# small-TU controller file (notebooknodecontroller_reorder.cpp) so we get the
# reorderNodes / moveNodes virtuals without dragging in the entire controller.
add_qt_test(test_notebook_node_view_reorder
  SOURCES
    test_notebook_node_view_reorder.cpp
    test_notebook_node_view_reorder_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodeview.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller_reorder.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooknodecontroller.h
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    Qt6::Widgets
    core_services
    vxcore
)
target_include_directories(test_notebook_node_view_reorder PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/controllers
)

# No VTextEdit.dll copy anchored here: test_notebook_node_view_reorder no longer
# links VTextEdit. The dir-wide copies live on test_navigationmodeservice and
# test_file_list_view_reorder, which do.

# Horizontal scrolling helper for the QTreeView family. Needs a real
# QApplication (QTEST_MAIN) so viewport geometry and indexAt() are meaningful.
add_qt_test(test_treehorizontalscroll
  SOURCES
    test_treehorizontalscroll.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/treehorizontalscrollhelper.cpp
  LINKS
    Qt6::Widgets
)

# NotebookNodeDelegate::sizeHint() width derivation. Pulls in the theme slice
# because the delegate resolves colors through ThemeService.
add_qt_test(test_notebooknodedelegate_sizehint
  SOURCES
    test_notebooknodedelegate_sizehint.cpp
    ${CMAKE_SOURCE_DIR}/src/views/notebooknodedelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/views/nodeiconhelper.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/commentcolorswatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/themeutils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    core_services
)

# TreeViewUtils is header-only (see src/gui/utils/treeviewutils.h), so this
# target needs no extra SOURCES and no extra LINKS. The five call sites are
# covered by the grep gate tests/utils/test_tree_indentation_drift.cpp.
add_qt_test(test_treeviewindentation
  SOURCES
    test_treeviewindentation.cpp
)

# vxpdf:// scheme handler: the MIME map and the document token registry. See the
# header comment of the .cpp for what requestStarted() coverage would require
# (a real QWebEngineUrlRequestJob, which has no public constructor).
if(QT_DEFAULT_MAJOR_VERSION GREATER 5 AND Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
  find_package(Qt6 REQUIRED COMPONENTS WebEngineCore)
  add_qt_test(test_vxpdfschemehandler
    SOURCES
      test_vxpdfschemehandler.cpp
      ${CMAKE_SOURCE_DIR}/src/gui/services/vxpdfschemehandler.cpp
    LINKS
      Qt6::WebEngineCore
      core_configs
    GUILESS
  )
  target_include_directories(test_vxpdfschemehandler PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/include
  )
  add_custom_command(TARGET test_vxpdfschemehandler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:VTextEdit>
      $<TARGET_FILE_DIR:test_vxpdfschemehandler>
  )
endif()