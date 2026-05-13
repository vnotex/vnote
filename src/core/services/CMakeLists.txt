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
