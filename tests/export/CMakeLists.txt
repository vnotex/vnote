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

# No VTextEdit.dll copy here: no target in tests/export links VTextEdit any more
# (core_services stopped linking it). Re-add the POST_BUILD copy from
# tests/AGENTS.md if a future test in this dir does link it.
