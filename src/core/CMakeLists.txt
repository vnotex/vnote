target_sources(vnote PRIVATE
    buffer/filetypehelper.cpp buffer/filetypehelper.h
    notebookbackend/inotebookbackend.cpp notebookbackend/inotebookbackend.h
    exampleplugin.cpp exampleplugin.h
    clipboarddata.cpp clipboarddata.h
    configmgr2.cpp configmgr2.h
    coreconfig.cpp coreconfig.h
    editorconfig.cpp editorconfig.h
    error.cpp error.h
    events.h
    exception.h
    exportcontext.h
    fileopensettings.h
    global.cpp global.h
    hookcontext.h
    hooknames.h
    iconfig.h
    iconfigmgr.h
    iviewwindowcontent.h
    location.h
    logger.cpp logger.h
    mainconfig.cpp mainconfig.h
    markdowneditorconfig.cpp markdowneditorconfig.h
    mindmapeditorconfig.cpp mindmapeditorconfig.h
    nodeinfo.h
    noncopyable.h
    vxcorelogbridge.cpp vxcorelogbridge.h
    pdfviewerconfig.cpp pdfviewerconfig.h
    searchresulttypes.h
    sessionconfig.cpp sessionconfig.h
    singleinstanceguard.cpp singleinstanceguard.h
    texteditorconfig.cpp texteditorconfig.h
    theme.cpp theme.h
    versioncontroller/iversioncontroller.h
    # DEPRECATED: VxCore singleton wrapper removed - use services via ServiceLocator instead
    webresource.h
    widgetconfig.cpp widgetconfig.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add services subdirectory as a separate library
add_subdirectory(services)
add_subdirectory(configs)
