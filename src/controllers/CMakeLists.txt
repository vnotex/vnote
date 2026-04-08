target_sources(vnote PRIVATE
    viewareacontroller.cpp viewareacontroller.h
    viewareaview.h
    workspacewrapper.cpp workspacewrapper.h
    notebooknodecontroller.cpp notebooknodecontroller.h
    newnotebookcontroller.cpp newnotebookcontroller.h
    newnotecontroller.cpp newnotecontroller.h
    newfoldercontroller.cpp newfoldercontroller.h
    opennotebookcontroller.cpp opennotebookcontroller.h
    managenotebookscontroller.cpp managenotebookscontroller.h
    recyclebincontroller.cpp recyclebincontroller.h
    importfoldercontroller.cpp importfoldercontroller.h
    tagcontroller.cpp tagcontroller.h
    snippetcontroller.cpp snippetcontroller.h
    textviewwindowcontroller.cpp textviewwindowcontroller.h
    pdfviewwindowcontroller.cpp pdfviewwindowcontroller.h
    mindmapviewwindowcontroller.cpp mindmapviewwindowcontroller.h
    markdowneditorcontroller.cpp markdowneditorcontroller.h
    markdownviewwindowcontroller.cpp markdownviewwindowcontroller.h
    outlinecontroller.cpp outlinecontroller.h
    attachmentcontroller.cpp attachmentcontroller.h
    exportcontroller.cpp exportcontroller.h
    searchcontroller.cpp searchcontroller.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
