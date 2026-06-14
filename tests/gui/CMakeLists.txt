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

# Copy VTextEdit DLL next to the new test executable so CTest can find it at
# runtime. The DLL lands in the shared Debug/ output dir used by all gui tests
# in this subdirectory, but anchoring on this target guarantees the copy fires
# even if other tests are skipped.
add_custom_command(TARGET test_notebook_node_view_reorder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebook_node_view_reorder>
)
