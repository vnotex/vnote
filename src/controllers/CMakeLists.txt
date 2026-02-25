target_sources(vnote PRIVATE
    notebooknodecontroller.cpp notebooknodecontroller.h
    newnotebookcontroller.cpp newnotebookcontroller.h
    newnotecontroller.cpp newnotecontroller.h
    newfoldercontroller.cpp newfoldercontroller.h
    opennotebookcontroller.cpp opennotebookcontroller.h
    managenotebookscontroller.cpp managenotebookscontroller.h
    recyclebincontroller.cpp recyclebincontroller.h
    importfoldercontroller.cpp importfoldercontroller.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
