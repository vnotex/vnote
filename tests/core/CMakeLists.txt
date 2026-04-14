# Core module tests
add_qt_test(test_error
  SOURCES
    test_error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  GUILESS
)

add_qt_test(test_exception
  SOURCES
    test_exception.cpp
  GUILESS
)

add_qt_test(test_servicelocator
  SOURCES
    test_servicelocator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/servicelocator.cpp
  GUILESS
)

add_qt_test(test_searchservice
  SOURCES
    test_searchservice.cpp
    ${CMAKE_SOURCE_DIR}/src/core/searchresulttypes.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_searchresulttypes
  SOURCES
    test_searchresulttypes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/searchresulttypes.cpp
  GUILESS
)

add_qt_test(test_searchresultmodel
  SOURCES
    test_searchresultmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/core/searchresulttypes.cpp
  LINKS
    Qt6::Widgets
  GUILESS
)

add_qt_test(test_configservice
  SOURCES
    test_configservice.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_notebookservice
  SOURCES
    test_notebookservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_hookmanager
  SOURCES
    test_hookmanager.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_hookintegration
  SOURCES
    test_hookintegration.cpp
    ${CMAKE_SOURCE_DIR}/src/core/exampleplugin.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_hookevents
  SOURCES
    test_hookevents.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_hookcontext
  SOURCES
    test_hookcontext.cpp
  GUILESS
)

add_qt_test(test_nodeinfo
  SOURCES
    test_nodeinfo.cpp
  GUILESS
)

add_qt_test(test_filetypeservice
  SOURCES
    test_filetypeservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_bufferservice
  SOURCES
    test_bufferservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_buffer
  SOURCES
    test_buffer.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_virtual_buffer
  SOURCES
    test_virtual_buffer.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_config_hook
  SOURCES
    test_config_hook.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_tagservice
  SOURCES
    test_tagservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_snippetservice
  SOURCES
    test_snippetservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_tagviewer2
  SOURCES
    test_tagviewer2.cpp
    stubs_tagviewer2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/tagviewer2.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/listwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/styleditemdelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/simplesegmenthighlighter.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/iconutils.cpp
  LINKS
    core_services
    vxcore
    Qt6::Widgets
)

# TagViewer2 widget sources need src/gui in include path (for utils/widgetutils.h).
target_include_directories(test_tagviewer2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_qt_test(test_textviewwindowcontroller
  SOURCES
    test_textviewwindowcontroller.cpp
    stubs_textviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/textviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/texteditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/markdowneditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pdfviewerconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mindmapeditorconfig.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
  GUILESS
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_textviewwindowcontroller POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_textviewwindowcontroller>
)

add_qt_test(test_pdfviewwindowcontroller
  SOURCES
    test_pdfviewwindowcontroller.cpp
    stubs_textviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/pdfviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pdfviewerconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/texteditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/markdowneditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mindmapeditorconfig.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
  GUILESS
)

add_custom_command(TARGET test_pdfviewwindowcontroller POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_pdfviewwindowcontroller>
)

add_qt_test(test_markdownviewwindowcontroller
  SOURCES
    test_markdownviewwindowcontroller.cpp
    stubs_textviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/markdownviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/texteditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/markdowneditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pdfviewerconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mindmapeditorconfig.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
    Qt6::Widgets
)

target_include_directories(test_markdownviewwindowcontroller PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_custom_command(TARGET test_markdownviewwindowcontroller POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_markdownviewwindowcontroller>
)

add_qt_test(test_markdowneditorcontroller
  SOURCES
    test_markdowneditorcontroller.cpp
    stubs_textviewwindowcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/markdowneditorcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/texteditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/markdowneditorconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pdfviewerconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mindmapeditorconfig.cpp
  LINKS
    core_services
    vxcore
    VTextEdit
  GUILESS
)

add_custom_command(TARGET test_markdowneditorcontroller POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_markdowneditorcontroller>
)

# TODO: test_configmgr2 requires many dependencies (MainConfig, SessionConfig,
# CoreConfig, EditorConfig, WidgetConfig, FileUtils2, Utils) that have deep
# cascading dependencies throughout the codebase. This test should be re-enabled
# once ConfigMgr2 dependencies are properly isolated into a testable library.
# add_qt_test(test_configmgr2
#   SOURCES
#     test_configmgr2.cpp
#     ${CMAKE_SOURCE_DIR}/src/core/configmgr2.cpp
#   LINKS
#     core_services
#     vxcore
#   GUILESS
# )

add_qt_test(test_widgetconfig_search
  SOURCES
    test_widgetconfig_search.cpp
    ${CMAKE_SOURCE_DIR}/src/core/widgetconfig.cpp
  GUILESS
)

add_qt_test(test_sessionconfig_search
  SOURCES
    test_sessionconfig_search.cpp
    stubs_sessionconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/sessionconfig.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_searchhighlightcontext
  SOURCES
    test_searchhighlightcontext.cpp
    ${CMAKE_SOURCE_DIR}/src/core/hookevents.cpp
  GUILESS
)

add_qt_test(test_quickaccessitem
  SOURCES
    test_quickaccessitem.cpp
    stubs_sessionconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/sessionconfig.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_mark_metadata
  SOURCES
    test_mark_metadata.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
