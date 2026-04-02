target_sources(vnote PRIVATE
    inodelistmodel.h
    notebooknodemodel.cpp notebooknodemodel.h
    notebooknodeproxymodel.cpp notebooknodeproxymodel.h
    searchresultmodel.cpp searchresultmodel.h
    outlinemodel.cpp outlinemodel.h
    tagfilemodel.cpp tagfilemodel.h
    tagmodel.cpp tagmodel.h
    attachmentlistmodel.cpp attachmentlistmodel.h
    snippetlistmodel.cpp snippetlistmodel.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
