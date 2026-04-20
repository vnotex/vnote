add_qt_test(test_findunitedentry
  SOURCES
    test_findunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/searchresulttypes.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/findunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/iunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/helpunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentryalias.h
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentrymgr.cpp
    stubs_findunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.h
  LINKS
    core_services
    vxcore
    Qt6::Widgets
  GUILESS
)

if(MSVC)
  target_compile_options(test_findunitedentry PRIVATE /FIQLabel)
else()
  target_compile_options(test_findunitedentry PRIVATE -include QLabel)
endif()

target_include_directories(test_findunitedentry PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

add_qt_test(test_unitedentry_history
  SOURCES
    test_unitedentry_history.cpp
    stubs_unitedentry_history.cpp
    ${CMAKE_SOURCE_DIR}/src/core/sessionconfig.cpp
  LINKS
    core_services
    vxcore
    Qt6::Widgets
  GUILESS
)
