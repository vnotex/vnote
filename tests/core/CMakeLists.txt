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

add_qt_test(test_searchservice
  SOURCES
    test_searchservice.cpp
  LINKS
    core_services
    vxcore
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

# TODO: test_configmgr2 requires many dependencies (MainConfig, SessionConfig,
# CoreConfig, EditorConfig, WidgetConfig, FileUtils2, Utils) that have deep
# cascading dependencies throughout the codebase. This test should be re-enabled
# once ConfigMgr2 dependencies are properly isolated into a testable library.
# add_qt_test(test_configmgr2
#   SOURCES
#     test_configmgr2.cpp
#     ${CMAKE_SOURCE_DIR}/src/core/configmgr2.cpp
#   LINKS
#     core_services
#     vxcore
#   GUILESS
# )
