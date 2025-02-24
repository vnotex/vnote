target_sources(vnote PRIVATE
    notebooknodecontroller.cpp notebooknodecontroller.h
    newnotebookcontroller.cpp newnotebookcontroller.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
