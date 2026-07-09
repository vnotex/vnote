add_qt_test(test_findunitedentry
  SOURCES
    test_findunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
    ${CMAKE_SOURCE_DIR}/src/models/searchresultmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/findunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/iunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/helpunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentryalias.h
    ${CMAKE_SOURCE_DIR}/src/utils/processutils.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentrymgr.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/historyunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/windowsunitedentry.h
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

# Copy VTextEdit DLL next to test executables so CTest can find it at runtime.
add_custom_command(TARGET test_findunitedentry POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_findunitedentry>
)

add_qt_test(test_unitedentry_history
  SOURCES
    test_unitedentry_history.cpp
  LINKS
    core_configs
    vxcore
    Qt6::Widgets
  GUILESS
)

add_qt_test(test_windowsunitedentry
  SOURCES
    test_windowsunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/windowsunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/iunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentrymgr.cpp
    ${CMAKE_SOURCE_DIR}/src/core/configmgr2.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/findunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/helpunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/historyunitedentry.h
    ${CMAKE_SOURCE_DIR}/src/unitedentry/unitedentryalias.h
    stubs_windowsunitedentry.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/services/themeservice.h
  LINKS
    core_services
    vxcore
    Qt6::Widgets
)

target_include_directories(test_windowsunitedentry PRIVATE
  ${CMAKE_SOURCE_DIR}/src/gui
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_windowsunitedentry POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_windowsunitedentry>
)
