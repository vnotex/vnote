target_sources(vnote PRIVATE
    buffer/buffer.cpp buffer/buffer.h
    buffer/bufferprovider.cpp buffer/bufferprovider.h
    buffer/filebufferprovider.cpp buffer/filebufferprovider.h
    buffer/filetypehelper.cpp buffer/filetypehelper.h
    buffer/ibufferfactory.h
    buffer/markdownbuffer.cpp buffer/markdownbuffer.h
    buffer/markdownbufferfactory.cpp buffer/markdownbufferfactory.h
    buffer/mindmapbuffer.cpp buffer/mindmapbuffer.h
    buffer/mindmapbufferfactory.cpp buffer/mindmapbufferfactory.h
    buffer/nodebufferprovider.cpp buffer/nodebufferprovider.h
    buffer/pdfbuffer.cpp buffer/pdfbuffer.h
    buffer/pdfbufferfactory.cpp buffer/pdfbufferfactory.h
    buffer/textbuffer.cpp buffer/textbuffer.h
    buffer/textbufferfactory.cpp buffer/textbufferfactory.h
    buffer/urlbasedbufferprovider.h
    buffermgr.cpp buffermgr.h
    clipboarddata.cpp clipboarddata.h
    configmgr.cpp configmgr.h
    coreconfig.cpp coreconfig.h
    editorconfig.cpp editorconfig.h
    events.h
    exception.h
    externalfile.cpp externalfile.h
    file.cpp file.h
    filelocator.h
    fileopenparameters.h
    global.cpp global.h
    historyitem.cpp historyitem.h
    historymgr.cpp historymgr.h
    htmltemplatehelper.cpp htmltemplatehelper.h
    iconfig.h
    location.h
    logger.cpp logger.h
    mainconfig.cpp mainconfig.h
    markdowneditorconfig.cpp markdowneditorconfig.h
    mindmapeditorconfig.cpp mindmapeditorconfig.h
    namebasedserver.h
    noncopyable.h
    notebook/bundlenotebook.cpp notebook/bundlenotebook.h
    notebook/bundlenotebookfactory.cpp notebook/bundlenotebookfactory.h
    notebook/externalnode.cpp notebook/externalnode.h
    notebook/historyi.h
    notebook/inotebookfactory.h
    notebook/node.cpp notebook/node.h
    notebook/nodeparameters.cpp notebook/nodeparameters.h
    notebook/notebook.cpp notebook/notebook.h
    notebook/notebookdatabaseaccess.cpp notebook/notebookdatabaseaccess.h
    notebook/notebookparameters.cpp notebook/notebookparameters.h
    notebook/notebooktagmgr.cpp notebook/notebooktagmgr.h
    notebook/tag.cpp notebook/tag.h
    notebook/tagi.h
    notebook/vxnode.cpp notebook/vxnode.h
    notebook/vxnodefile.cpp notebook/vxnodefile.h
    notebookbackend/inotebookbackend.cpp notebookbackend/inotebookbackend.h
    notebookbackend/inotebookbackendfactory.h
    notebookbackend/localnotebookbackend.cpp notebookbackend/localnotebookbackend.h
    notebookbackend/localnotebookbackendfactory.cpp notebookbackend/localnotebookbackendfactory.h
    notebookconfigmgr/bundlenotebookconfigmgr.cpp notebookconfigmgr/bundlenotebookconfigmgr.h
    notebookconfigmgr/inotebookconfigmgr.cpp notebookconfigmgr/inotebookconfigmgr.h
    notebookconfigmgr/inotebookconfigmgrfactory.h
    notebookconfigmgr/notebookconfig.cpp notebookconfigmgr/notebookconfig.h
    notebookconfigmgr/vxnodeconfig.cpp notebookconfigmgr/vxnodeconfig.h
    notebookconfigmgr/vxnotebookconfigmgr.cpp notebookconfigmgr/vxnotebookconfigmgr.h
    notebookconfigmgr/vxnotebookconfigmgrfactory.cpp notebookconfigmgr/vxnotebookconfigmgrfactory.h
    notebookmgr.cpp notebookmgr.h
    pdfviewerconfig.cpp pdfviewerconfig.h
    quickaccesshelper.cpp quickaccesshelper.h
    sessionconfig.cpp sessionconfig.h
    singleinstanceguard.cpp singleinstanceguard.h
    templatemgr.cpp templatemgr.h
    texteditorconfig.cpp texteditorconfig.h
    theme.cpp theme.h
    thememgr.cpp thememgr.h
    versioncontroller/dummyversioncontroller.cpp versioncontroller/dummyversioncontroller.h
    versioncontroller/dummyversioncontrollerfactory.cpp versioncontroller/dummyversioncontrollerfactory.h
    versioncontroller/iversioncontroller.h
    versioncontroller/iversioncontrollerfactory.h
    versioncontroller/versioncontrollerserver.cpp versioncontroller/versioncontrollerserver.h
    vnotex.cpp vnotex.h
    webresource.h
    widgetconfig.cpp widgetconfig.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
