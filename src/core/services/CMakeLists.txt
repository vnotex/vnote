# Core services library
set(SERVICES_SOURCES
  buffer2.cpp
  buffercoreservice.cpp
  bufferservice.cpp
  configcoreservice.cpp
  configservice.cpp
  searchcoreservice.cpp
  searchservice.cpp
  notebookcoreservice.cpp
  snippetcoreservice.cpp
  tagcoreservice.cpp
  tagservice.cpp
  filetypecoreservice.cpp
  templateservice.cpp
  htmltemplateservice.cpp
  hookmanager.cpp
  imagehostservice.cpp
  imagehostworker.cpp
  workspacecoreservice.cpp
  vnote3migrationservice.cpp
  synccredentialsstore.cpp
  ${CMAKE_SOURCE_DIR}/src/core/hookevents.cpp
  ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  ${CMAKE_SOURCE_DIR}/src/core/markdownwebglobaloptions.cpp
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
  configcoreservice.h
  configservice.h
  searchcoreservice.h
  searchservice.h
  notebookcoreservice.h
  snippetcoreservice.h
  tagcoreservice.h
  tagservice.h
  filetypecoreservice.h
  templateservice.h
  htmltemplateservice.h
  hookmanager.h
  imagehostservice.h
  imagehostworker.h
  workspacecoreservice.h
  vnote3migrationservice.h
  synccredentialsstore.h
)

add_library(core_services STATIC ${SERVICES_SOURCES} ${SERVICES_HEADERS})

target_link_libraries(core_services
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    vxcore
    VTextEdit
)

target_include_directories(core_services
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/vxcore/include
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
  if(TARGET Qt6Keychain::Qt6Keychain)
    target_link_libraries(core_services PRIVATE Qt6Keychain::Qt6Keychain)
  elseif(TARGET qt6keychain)
    target_link_libraries(core_services PRIVATE qt6keychain)
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
