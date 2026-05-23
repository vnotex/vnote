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

add_qt_test(test_theme_core
  SOURCES
    test_theme.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_searchservice
  SOURCES
    test_searchservice.cpp
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

add_qt_test(test_vnote3migrationservice
  SOURCES
    test_vnote3migrationservice.cpp
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
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.h
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
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
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
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
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
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
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
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
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

add_qt_test(test_configmgr2
  SOURCES
    test_configmgr2.cpp
  LINKS
    core_configs
    vxcore
  GUILESS
)

add_qt_test(test_widgetconfig_search
  SOURCES
    test_widgetconfig_search.cpp
    ${CMAKE_SOURCE_DIR}/src/core/widgetconfig.cpp
  GUILESS
)

add_qt_test(test_sessionconfig_search
  SOURCES
    test_sessionconfig_search.cpp
  LINKS
    core_configs
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
  LINKS
    core_configs
  GUILESS
)

add_qt_test(test_vxurl_parsing
  SOURCES
    test_vxurl_parsing.cpp
  GUILESS
)

add_qt_test(test_fileopensettings
  SOURCES
    test_fileopensettings.cpp
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

add_qt_test(test_sessionconfig_extprog
  SOURCES
    test_sessionconfig_extprog.cpp
  LINKS
    core_configs
  GUILESS
)

add_qt_test(test_imagehostservice
  SOURCES
    test_imagehostservice.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_customcommandprovider
  SOURCES
    test_customcommandprovider.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_imagehostuploadhook
  SOURCES
    test_imagehostuploadhook.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_imagehostasync
  SOURCES
    test_imagehostasync.cpp
  LINKS
    core_services
  GUILESS
)

add_qt_test(test_htmltemplateservice
  SOURCES
    test_htmltemplateservice.cpp
  LINKS
    core_services
    core_configs
  GUILESS
)

# T4: SyncCredentialsStore (QtKeychain wrapper, no plaintext fallback per ADR-9).
add_qt_test(test_synccredentialsstore
  SOURCES
    test_synccredentialsstore.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
# Reserve the ADR-6 test seam macro for future test-only code paths.
target_compile_definitions(test_synccredentialsstore PRIVATE VNOTE_TESTING)
# Mirror the keychain availability flag from core_services into the test so
# preprocessor checks (QSKIP guards) compile against the same code path.
if(VNOTE_KEYCHAIN_AVAILABLE)
  target_compile_definitions(test_synccredentialsstore PRIVATE VNOTE_KEYCHAIN_AVAILABLE)
endif()

# T5: NotebookCoreService sync wrapper methods (8 thin C-API wrappers).
add_qt_test(test_notebookcoreservice_sync
  SOURCES
    test_notebookcoreservice_sync.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
# Wave 7.1 (sync-backend-phase4): force-include the git backend translation
# unit so its self-registration token (kGitRegistration in git_sync_backend.cpp)
# is preserved by the MSVC linker. Without this, the linker drops the .obj
# from the static vxcore.lib (no externally-referenced symbols), the static
# init never runs, and SyncManager::EnableSyncImpl gets a null factory for
# backend "git". Other parent sync tests don't exercise the git factory and
# so don't need this.
if(MSVC)
  target_link_options(test_notebookcoreservice_sync PRIVATE /WHOLEARCHIVE:vxcore)
endif()

# T6: SyncWorker (worker-thread vxcore call dispatcher mirroring SearchService).
# Per ADR-1: SyncWorker calls only NotebookCoreService — never SyncManager directly.
# Per ADR-6: VNOTE_TESTING is reserved on the test target. Test seams on
# SyncWorker are unconditional (always compiled) to avoid duplicate-symbol
# issues that would arise from dual-compiling syncworker.cpp into both
# core_services and the test binary; this mirrors the T4 reservation pattern.
add_qt_test(test_syncworker
  SOURCES
    test_syncworker.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
target_compile_definitions(test_syncworker PRIVATE VNOTE_TESTING)

# T7: SyncService — GUI-thread facade owning a SyncWorker on a private QThread,
# wrapping SyncCredentialsStore. Per ADR-1 the service NEVER touches
# SyncManager directly. Per the T6 deviation pattern, test seams on
# SyncService (testSetInProgress) are unconditional public API; VNOTE_TESTING
# remains reserved on the test target so future test-only paths can compile-gate
# without dual-compiling syncservice.cpp into both libraries.
add_qt_test(test_syncservice
  SOURCES
    test_syncservice.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
target_compile_definitions(test_syncservice PRIVATE VNOTE_TESTING)
# Wave 7.1: same WHOLEARCHIVE rationale as test_notebookcoreservice_sync —
# this test exercises the git backend factory through SyncService.
if(MSVC)
  target_link_options(test_syncservice PRIVATE /WHOLEARCHIVE:vxcore)
endif()

# Task 8.1 (F3.1): SyncStateClassifier — pure read-through service that maps
# (notebook config + PAT presence + runtime registration) to one of S0..S7.
# Tests exercise the static classifyFromPredicates() helper so they don't
# need vxcore, keychain, or notebook fixtures.
add_qt_test(test_sync_classifier
  SOURCES
    test_sync_classifier.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

# Task 13.2 (F4.5): vnote.sync.* hook surface (before_enable, after_disable,
# conflict_detected, cancelled). Observe-only contract validation.
add_qt_test(test_sync_hooks
  SOURCES
    test_sync_hooks.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
if(MSVC)
  target_link_options(test_sync_hooks PRIVATE /WHOLEARCHIVE:vxcore)
endif()

# Task 13.4 (F1.6): SyncService::bootstrapAndPersist atomic enable+persist with
# rollback on persist failure. Verifies happy path, rollback on persist failure,
# and rollback failure logging.
add_qt_test(test_bootstrap_and_persist
  SOURCES
    test_bootstrap_and_persist.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)
if(MSVC)
  target_link_options(test_bootstrap_and_persist PRIVATE /WHOLEARCHIVE:vxcore)
endif()

# Task 14.1 (F5.1): SyncWorkQueueManager — per-notebook serialized executor.
# QThreadPool-based; per-notebook FIFO; parallel across notebooks.
add_qt_test(test_sync_work_queue_manager
  SOURCES
    test_sync_work_queue_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/services/syncworkqueuemanager.cpp
  LINKS
    Qt6::Core
  GUILESS
)
