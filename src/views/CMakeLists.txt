target_sources(vnote PRIVATE
    notebooknodedelegate.cpp notebooknodedelegate.h
    notebooknodeview.cpp notebooknodeview.h
    twocolumnsnodeexplorer.cpp twocolumnsnodeexplorer.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
