find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)

add_qt_test(test_snippetcontroller
  SOURCES
    test_snippetcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/snippetcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_searchcontroller
  SOURCES
    test_searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/searchcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/models/searchresultmodel.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_openvnote3notebookcontroller
  SOURCES
    test_openvnote3notebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/openvnote3notebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/opennotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_newnotebookcontroller
  SOURCES
    test_newnotebookcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/newnotebookcontroller.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_exportcontroller
  SOURCES
    test_exportcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/exportcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportdata.cpp
  LINKS
    core_services
    vxcore
    Qt6::Gui
    Qt6::Widgets
  GUILESS
)
