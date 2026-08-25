target_sources(vnote PRIVATE
    viewareacontroller.cpp viewareacontroller.h
    viewareaview.h
    workspacewrapper.cpp workspacewrapper.h
    notebooknodecontroller.cpp notebooknodecontroller.h
    notebooknodecontroller_reorder.cpp
    notebooknodecontroller_shareseam.cpp
    newnotebookcontroller.cpp newnotebookcontroller.h
    newnotecontroller.cpp newnotecontroller.h
    newfoldercontroller.cpp newfoldercontroller.h
    opennotebookcontroller.cpp opennotebookcontroller.h
    openvnote3notebookcontroller.cpp openvnote3notebookcontroller.h
    managenotebookscontroller.cpp managenotebookscontroller.h
    notebooksyncinfocontroller.cpp notebooksyncinfocontroller.h
    syncconflictcontroller.cpp syncconflictcontroller.h
    recyclebincontroller.cpp recyclebincontroller.h
    firstruncontroller.cpp firstruncontroller.h
    importfoldercontroller.cpp importfoldercontroller.h
    tagcontroller.cpp tagcontroller.h
    snippetcontroller.cpp snippetcontroller.h
    taskcontroller.cpp taskcontroller.h
    textviewwindowcontroller.cpp textviewwindowcontroller.h
    commentcontroller.cpp commentcontroller.h
    pdfviewwindowcontroller.cpp pdfviewwindowcontroller.h
    mindmapviewwindowcontroller.cpp mindmapviewwindowcontroller.h
    markdowneditorcontroller.cpp markdowneditorcontroller.h
    markdownviewwindowcontroller.cpp markdownviewwindowcontroller.h
    outlinecontroller.cpp outlinecontroller.h
    attachmentcontroller.cpp attachmentcontroller.h
    exportcontroller.cpp exportcontroller.h
    foldersharecontroller.cpp foldersharecontroller.h
    searchcontroller.cpp searchcontroller.h
    imagehostcontroller.cpp imagehostcontroller.h
    legacyimagemigrationcontroller.cpp legacyimagemigrationcontroller.h
    dashboardcontroller.cpp dashboardcontroller.h
    notificationrouter.cpp notificationrouter.h
    updatecontroller.cpp updatecontroller.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

