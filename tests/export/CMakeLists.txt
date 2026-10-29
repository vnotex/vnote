# GUI-free unit test for the export syntax-highlight style resolver (Task 3 invariant).
add_qt_test(test_exportstyleresolver
  SOURCES
    test_exportstyleresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportstyleresolver.cpp
  GUILESS
)

# GUI-free round-trip test for ExportPdfOption JSON serialization (header/footer fields).
add_qt_test(test_exportdata
  SOURCES
    test_exportdata.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportdata.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

# Real native rendering and final PDF transport, without the MarkdownViewer closure.
find_package(Qt6 REQUIRED COMPONENTS Network WebEngineWidgets)
set(_native_export_sources
  ${CMAKE_SOURCE_DIR}/src/export/exportrenderer.cpp
  ${CMAKE_SOURCE_DIR}/src/export/exportrendererfactory.cpp
  ${CMAKE_SOURCE_DIR}/src/export/mindmapexporter.cpp
  ${CMAKE_SOURCE_DIR}/src/export/htmlpdfexporter.cpp
  ${CMAKE_SOURCE_DIR}/src/export/exporter.cpp
  ${CMAKE_SOURCE_DIR}/src/export/exportstylesheet.cpp
  ${CMAKE_SOURCE_DIR}/src/export/exportstylesheet.qrc
  ${CMAKE_SOURCE_DIR}/src/export/exportfontresolver.cpp
  ${CMAKE_SOURCE_DIR}/src/export/wkhtmltopdfhtmlpatch.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/contentmediautils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/imagedestinationrewriter.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/htmlutils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/markdownhtmlrenderer.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/headingslugger.cpp
  ${CMAKE_SOURCE_DIR}/src/core/buffer/filetypehelper.cpp
  ${CMAKE_SOURCE_DIR}/src/gui/services/mindmapimageloader.cpp
  ${CMAKE_SOURCE_DIR}/src/gui/utils/imageutils.cpp
)
foreach(_export_test test_mindmapexport test_htmlpdfexporter)
  add_qt_test(${_export_test}
    SOURCES ${_export_test}.cpp ${_native_export_sources}
    LINKS core_services core_configs core_net VTextEdit m3::qt_editor m3::core
          Qt6::Widgets Qt6::Network Qt6::Svg Qt6::WebEngineWidgets
  )
  target_include_directories(${_export_test} PRIVATE ${CMAKE_SOURCE_DIR}/src/gui)
  set_tests_properties(${_export_test} PROPERTIES TIMEOUT 300)
  add_custom_command(TARGET ${_export_test} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:VTextEdit> $<TARGET_FILE_DIR:${_export_test}>
  )
  # Locate this Qt kit's renderer/resources, never an inherited deployed-app override.
  if(APPLE)
    # WebEngine needs Cocoa's native graphics context, not the offscreen plugin.
    set(_export_test_platform cocoa)
    set(_export_test_platform_plugin Qt6::QCocoaIntegrationPlugin)
  else()
    set(_export_test_platform offscreen)
    set(_export_test_platform_plugin Qt6::QOffscreenIntegrationPlugin)
  endif()
  set_property(TEST ${_export_test} PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM=${_export_test_platform}"
    "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:${_export_test_platform_plugin}>"
    "QTWEBENGINEPROCESS_PATH="
    "QTWEBENGINE_RESOURCES_PATH="
    "QTWEBENGINE_LOCALES_PATH="
    "QTWEBENGINE_CHROMIUM_FLAGS="
  )
  if(WIN32)
    set(_export_test_path "$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:VTextEdit>;$ENV{PATH}")
    string(REPLACE ";" "\;" _export_test_path "${_export_test_path}")
    set_property(TEST ${_export_test} APPEND PROPERTY ENVIRONMENT "PATH=${_export_test_path}")
  elseif(UNIX AND NOT APPLE)
    set_property(TEST ${_export_test} APPEND PROPERTY ENVIRONMENT
      "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Qt6::Core>:$<TARGET_FILE_DIR:VTextEdit>:$ENV{LD_LIBRARY_PATH}")
  endif()
endforeach()
unset(_native_export_sources)
unset(_export_test)
unset(_export_test_path)
unset(_export_test_platform)
unset(_export_test_platform_plugin)
