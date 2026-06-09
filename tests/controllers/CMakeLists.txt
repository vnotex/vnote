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
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
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

add_qt_test(test_syncconflictcontroller
  SOURCES
    test_syncconflictcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/syncconflictcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dialogs/syncconflictdialog2.cpp
  LINKS
    Qt6::Core
    Qt6::Widgets
    core_services
)
# QTEST_MAIN's default crash-handler hooks deadlock when ctest captures stdout
# via its pipe driver on Windows (per T14 learnings). Override the
# auto-registered ctest command to invoke the binary with `-nocrashhandler`.
set_property(TEST test_syncconflictcontroller PROPERTY
  COMMAND $<TARGET_FILE:test_syncconflictcontroller> -nocrashhandler)
set_tests_properties(test_syncconflictcontroller PROPERTIES
  TIMEOUT 120
)
# Reserve the ADR-6 test seam macro for future test-only code paths. Per T6/T7
# learnings: do NOT dual-compile syncservice.cpp/syncworker.cpp/synccredentialsstore.cpp
# into the test target — they come from core_services and dual-compilation
# produces duplicate-symbol link errors on Windows AND breaks the
# VNOTE_KEYCHAIN_AVAILABLE compile gate for QtKeychain.
target_compile_definitions(test_syncconflictcontroller PRIVATE VNOTE_TESTING)

# Copy VTextEdit DLL next to test executables so CTest can find it at runtime.
# Pick any test target to anchor the POST_BUILD command; the DLL lands in the
# shared output directory used by all controller tests.
add_custom_command(TARGET test_notebooksyncinfocontroller POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_notebooksyncinfocontroller>
)

add_qt_test(test_syncservice_credentials_cleanup
  SOURCES
    test_syncservice_credentials_cleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    Qt6::Core
    Qt6::Widgets
    core_services
    vxcore
)
# QTEST_MAIN's default crash-handler hooks deadlock when ctest captures stdout
# via its pipe driver on Windows (per T14 / test_bootstrap learnings). Override
# the auto-registered ctest command to invoke the binary with `-nocrashhandler`.
set_property(TEST test_syncservice_credentials_cleanup PROPERTY
  COMMAND $<TARGET_FILE:test_syncservice_credentials_cleanup> -nocrashhandler)
set_tests_properties(test_syncservice_credentials_cleanup PROPERTIES
  TIMEOUT 60
)

add_qt_test(test_notebooknodecontroller_multiselect
  SOURCES
    test_notebooknodecontroller_multiselect.cpp
  LINKS
    Qt6::Core
  GUILESS
)

add_qt_test(test_controllers_readonly_state
  SOURCES
    test_controllers_readonly_state.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/markdowneditorcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/textviewwindowcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

# T22 (open-notebook-remote-readonly): OpenNotebookController::cloneAndOpen
# async clone + staging-dir-safe rollback. Tests exercise the worker
# dispatch (QtConcurrent::run inside the controller), pre-flight validation,
# and the staging-dir cleanup contract using file:// bare-repo fixtures.
add_qt_test(test_open_notebook_controller_clone
  SOURCES
    test_open_notebook_controller_clone.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
    Qt6::Widgets
    core_services
    vxcore
)
# QTEST_MAIN crash-handler deadlocks with ctest's stdout pipe driver on
# Windows (per T14 / test_bootstrap learnings). Use -nocrashhandler.
set_property(TEST test_open_notebook_controller_clone PROPERTY
  COMMAND $<TARGET_FILE:test_open_notebook_controller_clone> -nocrashhandler)
set_tests_properties(test_open_notebook_controller_clone PROPERTIES
  TIMEOUT 180
)
if(MSVC)
  # vxcore self-registers the git backend via a token in
  # git_sync_backend.cpp. MSVC's /OPT:REF drops the .obj if no externally
  # referenced symbol pulls it in -- mirror the trick used by
  # test_notebook_core_service_clone so vxcore_sync_clone resolves "git".
  target_link_options(test_open_notebook_controller_clone PRIVATE /WHOLEARCHIVE:vxcore)
endif()

# T23 (open-notebook-remote-readonly): OpenNotebookInput.readOnly param.
# Verifies the controller forwards the new struct field to
# NotebookCoreService::openNotebookEx with the correct options JSON
# ({"readOnly":true} vs "{}"). One subtest per branch + back-compat
# verification via vxcore_notebook_is_read_only.
add_qt_test(test_open_notebook_controller_readonly_param
  SOURCES
    test_open_notebook_controller_readonly_param.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Concurrent
    core_services
    vxcore
  GUILESS
)
