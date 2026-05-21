# Integration test targets registered by individual tasks. See T16, F3.

# T16: end-to-end conflict resolution flow + retry cap test.
# Dual-compiles SyncConflictController + SyncConflictDialog2 (small) but uses
# the real SyncService/SyncCredentialsStore from core_services (per T6/T7
# duplicate-symbol guidance).
add_qt_test(test_sync_e2e
  SOURCES
    test_sync_e2e.cpp
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
set_property(TEST test_sync_e2e PROPERTY
  COMMAND $<TARGET_FILE:test_sync_e2e> -nocrashhandler)
set_tests_properties(test_sync_e2e PROPERTIES
  TIMEOUT 120
)
# Reserve the ADR-6 test seam macro for future test-only code paths. Per T6/T7
# learnings: do NOT dual-compile syncservice.cpp/syncworker.cpp/synccredentialsstore.cpp
# into the test target — they come from core_services.
target_compile_definitions(test_sync_e2e PRIVATE VNOTE_TESTING)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_sync_e2e POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_sync_e2e>
)

# W5.T1+W5.T2 test_sync_state_machine retired by T27 (sync-queue-convergence):
# the matrix relied on the deleted SyncWorker class. State-machine coverage now
# lives in test_sync_signal_baseline + test_sync_signal_auto_baseline +
# test_sync_ops + test_sync_close_block; cold-start recovery is exercised by
# test_bootstrap_and_persist.

# W5.T3: credential secrecy audit (no PAT in logs/JSON/source).
# Wraps scripts/test_pat_secrecy.ps1 so it runs as part of `ctest`.
if(WIN32)
  add_test(
    NAME test_pat_secrecy
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
      ${CMAKE_SOURCE_DIR}/scripts/test_pat_secrecy.ps1
      -RepoRoot ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(test_pat_secrecy PROPERTIES TIMEOUT 60)
endif()

