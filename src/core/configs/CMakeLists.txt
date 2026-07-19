# Core configs library (for test linking)
set(CONFIGS_SOURCES
  ${CMAKE_SOURCE_DIR}/src/core/configmgr2.cpp
  ${CMAKE_SOURCE_DIR}/src/core/mainconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/sessionconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/coreconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/editorconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/widgetconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/texteditorconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/markdowneditorconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/pdfviewerconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/mindmapeditorconfig.cpp
  ${CMAKE_SOURCE_DIR}/src/core/global.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
  ${CMAKE_SOURCE_DIR}/src/export/exportdata.cpp
  ${CMAKE_SOURCE_DIR}/src/search/searchdata.cpp
)

add_library(core_configs STATIC ${CONFIGS_SOURCES})

target_link_libraries(core_configs
  PUBLIC
    core_services
    VTextEdit
)

target_include_directories(core_configs
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/vxcore/include
)

set_target_properties(core_configs PROPERTIES
  AUTOMOC ON
)
