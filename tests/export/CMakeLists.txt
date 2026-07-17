# GUI-free unit test for the export syntax-highlight style resolver (Task 3 invariant).
add_qt_test(test_exportstyleresolver
  SOURCES
    test_exportstyleresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportstyleresolver.cpp
  GUILESS
)

# New-subdir requirement (tests/AGENTS.md): copy VTextEdit.dll next to the test exes so any
# future VTextEdit-linking test in this dir resolves it at runtime. Anchored on this target.
add_custom_command(TARGET test_exportstyleresolver POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_exportstyleresolver>
)
