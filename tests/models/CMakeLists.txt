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

# Copy VTextEdit DLL next to test executables so CTest can find it at runtime.
# Pick any test target to anchor the POST_BUILD command; the DLL lands in the
# shared output directory used by all model tests in this subdirectory.
add_custom_command(TARGET test_tagfilemodel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_tagfilemodel>
)
