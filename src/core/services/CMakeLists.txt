# Core services library
set(SERVICES_SOURCES
  activityservice.cpp
  activitystatsservice.cpp
  buffer2.cpp
  buffercoreservice.cpp
  bufferservice.cpp
  buffersavequeue.cpp
  configcoreservice.cpp
  configservice.cpp
  searchcoreservice.cpp
  searchservice.cpp
  notebookcoreservice.cpp
  notebookiogate.cpp
  historyservice.cpp
  snippetcoreservice.cpp
  tagcoreservice.cpp
  tagservice.cpp
  filetypecoreservice.cpp
  filetype.cpp
  templateservice.cpp
  htmltemplateservice.cpp
  hookmanager.cpp
  imagehostservice.cpp
  imagehostworker.cpp
  workspacecoreservice.cpp
  shellexecution.cpp
  task.cpp
  taskservice.cpp
  taskvariablemgr.cpp
  mainwindowtaskcontext.cpp
  vnote3migrationservice.cpp
  synccredentialsstore.cpp
  syncops.cpp
  syncservice.cpp
  syncstateclassifier.cpp
  syncerrorpresenter.cpp
  syncworkqueuemanager.cpp
  eventbridge.cpp
  ${CMAKE_SOURCE_DIR}/src/core/hookevents.cpp
  ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  ${CMAKE_SOURCE_DIR}/src/core/logging.cpp
  ${CMAKE_SOURCE_DIR}/src/core/markdownwebglobaloptions.cpp
  ${CMAKE_SOURCE_DIR}/src/core/htmltemplateutils.cpp
  ${CMAKE_SOURCE_DIR}/src/core/searchresulttypes.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/fileutils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/webutils.cpp
  ${CMAKE_SOURCE_DIR}/src/imagehost/iimagehostprovider.cpp
  ${CMAKE_SOURCE_DIR}/src/imagehost/customcommandprovider.cpp
  ${CMAKE_SOURCE_DIR}/src/imagehost/githubprovider.cpp
  ${CMAKE_SOURCE_DIR}/src/imagehost/giteeprovider.cpp
)

set(SERVICES_HEADERS
  buffer2.h
  buffercoreservice.h
  bufferservice.h
  buffersavequeue.h
  activityservice.h
  activitystatsservice.h
  ibuffercoreservice.h
  isyncnotebookservice.h
  configcoreservice.h
  configservice.h
  searchcoreservice.h
  searchservice.h
  notebookcoreservice.h
  notebookiogate.h
  historyservice.h
  snippetcoreservice.h
  tagcoreservice.h
  tagservice.h
  filetypecoreservice.h
  filetype.h
  templateservice.h
  htmltemplateservice.h
  hookmanager.h
  imagehostservice.h
  imagehostworker.h
  workspacecoreservice.h
  shellexecution.h
  task.h
  taskservice.h
  taskvariablemgr.h
  itaskcontext.h
  mainwindowtaskcontext.h
  vnote3migrationservice.h
  synccredentialsstore.h
  syncops.h
  syncservice.h
  syncstateclassifier.h
  syncerrorpresenter.h
  syncworkqueuemanager.h
  synclog.h
  eventbridge.h
  ${CMAKE_SOURCE_DIR}/src/core/logging.h
)

add_library(core_services STATIC ${SERVICES_SOURCES} ${SERVICES_HEADERS})

# Task::decodeText uses QTextCodec, which lives in Qt6::Core5Compat.
if(QT_DEFAULT_MAJOR_VERSION GREATER 5 AND TARGET Qt6::Core5Compat)
  target_link_libraries(core_services PUBLIC Qt6::Core5Compat)
endif()

target_link_libraries(core_services
  PUBLIC
    Qt${QT_DEFAULT_MAJOR_VERSION}::Core
    Qt${QT_DEFAULT_MAJOR_VERSION}::Gui
    Qt${QT_DEFAULT_MAJOR_VERSION}::Network
    Qt${QT_DEFAULT_MAJOR_VERSION}::Concurrent
    vxcore
    VTextEdit
  PRIVATE
    # T6 (notebook-explorer-drag-reorder): NotebookCoreService::buildOrderedJson
    # uses nlohmann::json to build the {folders, files} payload consumed by
    # vxcore_folder_set_children_order. The target is the INTERFACE library
    # exposed by libs/vxcore/third_party/CMakeLists.txt.
    nlohmann_json
)

target_include_directories(core_services
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/vxcore/include
    ${CMAKE_SOURCE_DIR}/libs/vxcore/src
    ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/include
)

set_target_properties(core_services PROPERTIES
  AUTOMOC ON
)

# QtKeychain integration for SyncCredentialsStore (per ADR-9, no plaintext fallback).
# VNOTE_KEYCHAIN_AVAILABLE is set by the top-level CMakeLists.txt when QtKeychain
# is found (system find_package OR FetchContent build).
if(VNOTE_KEYCHAIN_AVAILABLE)
  target_compile_definitions(core_services PRIVATE VNOTE_KEYCHAIN_AVAILABLE)
  if(QT_DEFAULT_MAJOR_VERSION GREATER 5)
    if(TARGET Qt6Keychain::Qt6Keychain)
      target_link_libraries(core_services PRIVATE Qt6Keychain::Qt6Keychain)
    elseif(TARGET qt6keychain)
      target_link_libraries(core_services PRIVATE qt6keychain)
    endif()
  else()
    if(TARGET Qt5Keychain::Qt5Keychain)
      target_link_libraries(core_services PRIVATE Qt5Keychain::Qt5Keychain)
    elseif(TARGET qt5keychain)
      target_link_libraries(core_services PRIVATE qt5keychain)
    endif()
  endif()
  # FetchContent path: QtKeychain's CMakeLists.txt only sets INSTALL_INTERFACE
  # include directories, so consumers via add_subdirectory / FetchContent must
  # add the source + binary dirs explicitly to find <keychain.h> and
  # <qkeychain_export.h>. System find_package path already propagates includes
  # via the imported target's INTERFACE_INCLUDE_DIRECTORIES.
  if(qtkeychain_SOURCE_DIR)
    target_include_directories(core_services PRIVATE
      ${qtkeychain_SOURCE_DIR}
      ${qtkeychain_BINARY_DIR}
    )
  endif()
endif()
