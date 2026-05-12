find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)

add_qt_test(test_snippetcontroller
  SOURCES
    test_snippetcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/snippetcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_searchcontroller
  SOURCES
    test_searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultmodel.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_openvnote3notebookcontroller
  SOURCES
    test_openvnote3notebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/openvnote3notebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_newnotebookcontroller
  SOURCES
    test_newnotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_bootstrap
  SOURCES
    test_bootstrap.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    core_services
    vxcore
    Qt6::Widgets
)
# QTEST_MAIN's default crash-handler hooks deadlock when ctest captures stdout
# via its pipe driver on Windows. Override the auto-registered ctest command
# to invoke the binary with `-nocrashhandler`, which sidesteps the hang.
set_property(TEST test_bootstrap PROPERTY
  COMMAND $<TARGET_FILE:test_bootstrap> -nocrashhandler)
set_tests_properties(test_bootstrap PROPERTIES
  TIMEOUT 120
)

add_qt_test(test_exportcontroller
  SOURCES
    test_exportcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/exportcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportdata.cpp
  LINKS
    core_services
    vxcore
    Qt6::Gui
    Qt6::Widgets
  GUILESS
)

add_qt_test(test_notebooksyncinfocontroller
  SOURCES
    test_notebooksyncinfocontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/notebooksyncinfocontroller.cpp
  LINKS
    Qt6::Core
    core_services
    vxcore
  GUILESS
)
# Reserve the ADR-6 test seam macro for future test-only code paths. Per T6/T7
# learnings: do NOT dual-compile syncservice.cpp/syncworker.cpp/synccredentialsstore.cpp
# into the test target — they come from core_services and dual-compilation
# produces duplicate-symbol link errors on Windows AND breaks the
# VNOTE_KEYCHAIN_AVAILABLE compile gate for QtKeychain. The test exercises the
# real services via core_services, which is exactly what ADR-6 asks for.
target_compile_definitions(test_notebooksyncinfocontroller PRIVATE VNOTE_TESTING)
