add_executable(VNote main.cpp
               vmainwindow.cpp
               vdirectorytree.cpp
               vnote.cpp
               vnotebook.cpp
               dialog/vnewdirdialog.cpp
               vconfigmanager.cpp
               vfilelist.cpp
               dialog/vnewfiledialog.cpp
               vedit.cpp
               vdocument.cpp
               utils/vutils.cpp
               vpreviewpage.cpp
               vstyleparser.cpp
               dialog/vnewnotebookdialog.cpp
               vmarkdownconverter.cpp
               dialog/vnotebookinfodialog.cpp
               dialog/vdirinfodialog.cpp
               dialog/vfileinfodialog.cpp
               veditoperations.cpp
               vmdeditoperations.cpp
               dialog/vinsertimagedialog.cpp
               vdownloader.cpp
               veditarea.cpp
               veditwindow.cpp
               vedittab.cpp
               voutline.cpp
               vsingleinstanceguard.cpp
               vdirectory.cpp
               vfile.cpp
               vnotebookselector.cpp
               vnofocusitemdelegate.cpp
               vmdedit.cpp
               dialog/vfindreplacedialog.cpp
               dialog/vsettingsdialog.cpp
               dialog/vdeletenotebookdialog.cpp
               dialog/vselectdialog.cpp
               vcaptain.cpp
               vopenedlistmenu.cpp
               vnavigationmode.cpp
               vorphanfile.cpp
               vcodeblockhighlighthelper.cpp
               vwebview.cpp
               vmdtab.cpp
               vhtmltab.cpp
               utils/vvim.cpp
               utils/veditutils.cpp
               vvimindicator.cpp
               vbuttonwithwidget.cpp
               vtabindicator.cpp
               dialog/vupdater.cpp
               dialog/vorphanfileinfodialog.cpp
               vtextblockdata.cpp
               utils/vpreviewutils.cpp
               dialog/vconfirmdeletiondialog.cpp
               vnotefile.cpp
               vattachmentlist.cpp
               dialog/vsortdialog.cpp
               vfilesessioninfo.cpp
               vtableofcontent.cpp
               utils/vmetawordmanager.cpp
               vmetawordlineedit.cpp
               dialog/vinsertlinkdialog.cpp
               vplaintextedit.cpp
               vimageresourcemanager.cpp
               vlinenumberarea.cpp
               veditor.cpp
               vmdeditor.cpp
               veditconfig.cpp
               vpreviewmanager.cpp
               vimageresourcemanager2.cpp
               vtextdocumentlayout.cpp
               vtextedit.cpp
               vsnippetlist.cpp
               vsnippet.cpp
               dialog/veditsnippetdialog.cpp
               utils/vimnavigationforwidget.cpp
               vtoolbox.cpp
               vinsertselector.cpp
               utils/vclipboardutils.cpp
               vpalette.cpp
               vbuttonmenuitem.cpp
               utils/viconutils.cpp
               lineeditdelegate.cpp
               dialog/vtipsdialog.cpp
               dialog/vcopytextashtmldialog.cpp
               vwaitingwidget.cpp
               utils/vwebutils.cpp
               vlineedit.cpp
               vcart.cpp
               vvimcmdlineedit.cpp
               vlistwidget.cpp
               vsimplesearchinput.cpp
               vstyleditemdelegate.cpp
               vtreewidget.cpp
               dialog/vexportdialog.cpp
               vexporter.cpp
               vsearcher.cpp
               vsearch.cpp
               vsearchresulttree.cpp
               vsearchengine.cpp
               vuniversalentry.cpp
               vlistwidgetdoublerows.cpp
               vdoublerowitemwidget.cpp
               vsearchue.cpp
               voutlineue.cpp
               vhelpue.cpp
               vlistfolderue.cpp
               dialog/vfixnotebookdialog.cpp
               vplantumlhelper.cpp
               vgraphvizhelper.cpp
               vlivepreviewhelper.cpp
               vmathjaxpreviewhelper.cpp
               vmathjaxwebdocument.cpp
               vmathjaxinplacepreviewhelper.cpp
               vhistorylist.cpp
               vexplorer.cpp
               vlistue.cpp
               vuetitlecontentpanel.cpp
               utils/vprocessutils.cpp
               vtagpanel.cpp
               valltagspanel.cpp
               vtaglabel.cpp
               vtagexplorer.cpp
               pegmarkdownhighlighter.cpp
               pegparser.cpp
               peghighlighterresult.cpp
               vtexteditcompleter.cpp
               utils/vkeyboardlayoutmanager.cpp
               dialog/vkeyboardlayoutmappingdialog.cpp
               vfilelistwidget.cpp
               widgets/vcombobox.cpp
               vtablehelper.cpp
               vtable.cpp
               dialog/vinserttabledialog.cpp
               isearchengine.cpp
               iuniversalentry.cpp
               vimagehosting.cpp
               vnote.qrc translations.qrc)

# Qt5 libraries
target_link_libraries(VNote PRIVATE Qt5::Core Qt5::WebEngine Qt5::WebEngineWidgets
                      Qt5::Network Qt5::PrintSupport Qt5::WebChannel Qt5::Widgets
                      Qt5::PrintSupport Qt5::Svg)
set_property(TARGET VNote PROPERTY AUTORCC_OPTIONS "--compress;9")

# Thirdparty libraries
target_include_directories(VNote PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/peg-highlight ${CMAKE_SOURCE_DIR}/hoedown)
target_link_libraries(VNote PRIVATE peg-highlight hoedown)

# Compile options
if(GCC_VERSION VERSION_GREATER_EQUAL 8.0)
  target_compile_options(VNote PRIVATE "-Wno-class-memaccess")
endif()

## INSTALLS
install(TARGETS VNote RUNTIME DESTINATION bin)
install(FILES translations/vnote_zh_CN.qm translations/vnote_ja.qm DESTINATION translations )

if(UNIX AND NOT DARWIN)
    set(desktop.path applications)
    set(desktop.files vnote.desktop)

    set(icon16.path icons/hicolor/16x16/apps)
    set(icon16.files resources/icons/16x16/vnote.png)

    set(icon32.path icons/hicolor/32x32/apps)
    set(icon32.files resources/icons/32x32/vnote.png)

    set(icon48.path icons/hicolor/48x48/apps)
    set(icon48.files resources/icons/48x48/vnote.png)

    set(icon64.path icons/hicolor/64x64/apps)
    set(icon64.files resources/icons/64x64/vnote.png)

    set(icon128.path icons/hicolor/128x128/apps)
    set(icon128.files resources/icons/128x128/vnote.png)

    set(icon256.path icons/hicolor/256x256/apps)
    set(icon256.files resources/icons/256x256/vnote.png)

    set(iconsvg.path icons/hicolor/scalable/apps)
    set(iconsvg.files resources/icons/vnote.svg)

    foreach(items IN ITEMS desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${${items}.files}
                DESTINATION share/${${items}.path}
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION share/doc/vnote/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        RENAME copyright)
elseif(DARWIN)
    set(MACOSX_BUNDLE_BUNDLE_NAME "VNote")
    set(MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.tamlok.VNote")
    set(MACOSX_BUNDLE_ICON_FILE ${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.icns)
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING ${MACOSX_BUNDLE_BUNDLE_VERSION})
    # Set short version independent with project version to be able to increment independendently.
    math(EXPR SHORT_VERSION_MAJOR "${PROJECT_VERSION_MAJOR} * 100 + ${PROJECT_VERSION_MINOR}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${SHORT_VERSION_MAJOR}.${PROJECT_VERSION_PATCH}.0")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "VNote")
    set(MACOSX_BUNDLE_COPYRIGHT "Distributed under MIT license. Copyright 2016-2019 Le Tan")
    set(MACOSX_BUNDLE_INFO_STRING "VNote is a note-taking application that knows programmers and Markdown better. Distributed under MIT license. Copyright 2017 Le Tan")

    set_source_files_properties(${MACOSX_BUNDLE_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/Packaging.cmake)
