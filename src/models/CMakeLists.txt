target_sources(vnote PRIVATE
    inodelistmodel.h
    notebooknodemodel.cpp notebooknodemodel.h
    notebooknodeproxymodel.cpp notebooknodeproxymodel.h
    outlinemodel.cpp outlinemodel.h
    tagmodel.cpp tagmodel.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
