add_qt_test(test_tagfilemodel
  SOURCES
    test_tagfilemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tagfilemodel.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_attachmentmodel
  SOURCES
    test_attachmentmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/attachmentlistmodel.cpp
  LINKS
    core_services
    vxcore
    Qt6::Widgets
  GUILESS
)

add_qt_test(test_snippetmodel
  SOURCES
    test_snippetmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/snippetlistmodel.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_historylistmodel
  SOURCES
    test_historylistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/historylistmodel.cpp
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_notebook_node_proxy_model_order
  SOURCES
    test_notebook_node_proxy_model_order.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodeproxymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    vxcore
  GUILESS
)

add_qt_test(test_notebooknodemodel
  SOURCES
    test_notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/notebooknodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  LINKS
    core_services
    vxcore
  GUILESS
)

# No VTextEdit.dll copy here: no target in tests/models links VTextEdit any more
# (core_services stopped linking it). Re-add the POST_BUILD copy from
# tests/AGENTS.md if a future test in this dir does link it.
