target_sources(vnote PRIVATE
    notebooknodemodel.cpp notebooknodemodel.h
    notebooknodeproxymodel.cpp notebooknodeproxymodel.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
