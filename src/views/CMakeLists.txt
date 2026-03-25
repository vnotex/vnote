target_sources(vnote PRIVATE
    notebooknodedelegate.cpp notebooknodedelegate.h
    filenodedelegate.cpp filenodedelegate.h
    filelistview.cpp filelistview.h
    notebooknodeview.cpp notebooknodeview.h
    outlineview.cpp outlineview.h
    inodeexplorer.h
    combinednodeexplorer.cpp combinednodeexplorer.h
    twocolumnsnodeexplorer.cpp twocolumnsnodeexplorer.h
    tagview.cpp tagview.h
    tagnodelistview.cpp tagnodelistview.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
