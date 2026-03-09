# Core services library
set(SERVICES_SOURCES
  bufferservice.cpp
  configservice.cpp
  searchservice.cpp
  notebookservice.cpp
  filetypeservice.cpp
  templateservice.cpp
  hookmanager.cpp
  ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/fileutils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
)

set(SERVICES_HEADERS
  bufferservice.h
  configservice.h
  searchservice.h
  notebookservice.h
  filetypeservice.h
  templateservice.h
  hookmanager.h
)

add_library(core_services STATIC ${SERVICES_SOURCES} ${SERVICES_HEADERS})

target_link_libraries(core_services
  PUBLIC
    Qt6::Core
    Qt6::Gui
    vxcore
)

target_include_directories(core_services
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/vxcore/include
)

set_target_properties(core_services PROPERTIES
  AUTOMOC ON
)
