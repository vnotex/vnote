#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui webenginewidgets webchannel network svg printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Enable message log in release build
DEFINES += QT_MESSAGELOGCONTEXT

TARGET = VNote
TEMPLATE = app

RC_ICONS = resources/icons/vnote.ico
ICON = resources/icons/vnote.icns

TRANSLATIONS += translations/vnote_zh_CN.ts \
                translations/vnote_ja.ts

*-g++ {
    QMAKE_CFLAGS_WARN_ON += -Wno-class-memaccess
    QMAKE_CXXFLAGS_WARN_ON += -Wno-class-memaccess
    QMAKE_CFLAGS += -Wno-class-memaccess
    QMAKE_CXXFLAGS += -Wno-class-memaccess
}

SOURCES += main.cpp\
    vapplication.cpp \
    vimagehosting.cpp \
    vmainwindow.cpp \
    vdirectorytree.cpp \
    vnote.cpp \
    vnotebook.cpp \
    dialog/vnewdirdialog.cpp \
    vconfigmanager.cpp \
    vfilelist.cpp \
    dialog/vnewfiledialog.cpp \
    vedit.cpp \
    vdocument.cpp \
    utils/vutils.cpp \
    vpreviewpage.cpp \
    vstyleparser.cpp \
    dialog/vnewnotebookdialog.cpp \
    vmarkdownconverter.cpp \
    dialog/vnotebookinfodialog.cpp \
    dialog/vdirinfodialog.cpp \
    dialog/vfileinfodialog.cpp \
    veditoperations.cpp \
    vmdeditoperations.cpp \
    dialog/vinsertimagedialog.cpp \
    vdownloader.cpp \
    veditarea.cpp \
    veditwindow.cpp \
    vedittab.cpp \
    voutline.cpp \
    vsingleinstanceguard.cpp \
    vdirectory.cpp \
    vfile.cpp \
    vnotebookselector.cpp \
    vnofocusitemdelegate.cpp \
    vmdedit.cpp \
    dialog/vfindreplacedialog.cpp \
    dialog/vsettingsdialog.cpp \
    dialog/vdeletenotebookdialog.cpp \
    dialog/vselectdialog.cpp \
    vcaptain.cpp \
    vopenedlistmenu.cpp \
    vnavigationmode.cpp \
    vorphanfile.cpp \
    vcodeblockhighlighthelper.cpp \
    vwebview.cpp \
    vmdtab.cpp \
    vhtmltab.cpp \
    utils/vvim.cpp \
    utils/veditutils.cpp \
    vvimindicator.cpp \
    vbuttonwithwidget.cpp \
    vtabindicator.cpp \
    dialog/vupdater.cpp \
    dialog/vorphanfileinfodialog.cpp \
    vtextblockdata.cpp \
    utils/vpreviewutils.cpp \
    dialog/vconfirmdeletiondialog.cpp \
    vnotefile.cpp \
    vattachmentlist.cpp \
    dialog/vsortdialog.cpp \
    vfilesessioninfo.cpp \
    vtableofcontent.cpp \
    utils/vmetawordmanager.cpp \
    vmetawordlineedit.cpp \
    dialog/vinsertlinkdialog.cpp \
    vplaintextedit.cpp \
    vimageresourcemanager.cpp \
    vlinenumberarea.cpp \
    veditor.cpp \
    vmdeditor.cpp \
    veditconfig.cpp \
    vpreviewmanager.cpp \
    vimageresourcemanager2.cpp \
    vtextdocumentlayout.cpp \
    vtextedit.cpp \
    vsnippetlist.cpp \
    vsnippet.cpp \
    dialog/veditsnippetdialog.cpp \
    utils/vimnavigationforwidget.cpp \
    vtoolbox.cpp \
    vinsertselector.cpp \
    utils/vclipboardutils.cpp \
    vpalette.cpp \
    vbuttonmenuitem.cpp \
    utils/viconutils.cpp \
    lineeditdelegate.cpp \
    dialog/vtipsdialog.cpp \
    dialog/vcopytextashtmldialog.cpp \
    vwaitingwidget.cpp \
    utils/vwebutils.cpp \
    vlineedit.cpp \
    vcart.cpp \
    vvimcmdlineedit.cpp \
    vlistwidget.cpp \
    vsimplesearchinput.cpp \
    vstyleditemdelegate.cpp \
    vtreewidget.cpp \
    dialog/vexportdialog.cpp \
    vexporter.cpp \
    vsearcher.cpp \
    vsearch.cpp \
    vsearchresulttree.cpp \
    vsearchengine.cpp \
    vuniversalentry.cpp \
    vlistwidgetdoublerows.cpp \
    vdoublerowitemwidget.cpp \
    vsearchue.cpp \
    voutlineue.cpp \
    vhelpue.cpp \
    vlistfolderue.cpp \
    dialog/vfixnotebookdialog.cpp \
    vplantumlhelper.cpp \
    vgraphvizhelper.cpp \
    vlivepreviewhelper.cpp \
    vmathjaxpreviewhelper.cpp \
    vmathjaxwebdocument.cpp \
    vmathjaxinplacepreviewhelper.cpp \
    vhistorylist.cpp \
    vexplorer.cpp \
    vlistue.cpp \
    vuetitlecontentpanel.cpp \
    utils/vprocessutils.cpp \
    vtagpanel.cpp \
    valltagspanel.cpp \
    vtaglabel.cpp \
    vtagexplorer.cpp \
    pegmarkdownhighlighter.cpp \
    pegparser.cpp \
    peghighlighterresult.cpp \
    vtexteditcompleter.cpp \
    utils/vkeyboardlayoutmanager.cpp \
    dialog/vkeyboardlayoutmappingdialog.cpp \
    vfilelistwidget.cpp \
    widgets/vcombobox.cpp \
    vtablehelper.cpp \
    vtable.cpp \
    dialog/vinserttabledialog.cpp

HEADERS  += vmainwindow.h \
    vapplication.h \
    vdirectorytree.h \
    vimagehosting.h \
    vnote.h \
    vnotebook.h \
    dialog/vnewdirdialog.h \
    vconfigmanager.h \
    vfilelist.h \
    dialog/vnewfiledialog.h \
    vedit.h \
    vconstants.h \
    vdocument.h \
    utils/vutils.h \
    vpreviewpage.h \
    vstyleparser.h \
    dialog/vnewnotebookdialog.h \
    vmarkdownconverter.h \
    dialog/vnotebookinfodialog.h \
    dialog/vdirinfodialog.h \
    dialog/vfileinfodialog.h \
    veditoperations.h \
    vmdeditoperations.h \
    dialog/vinsertimagedialog.h \
    vdownloader.h \
    veditarea.h \
    veditwindow.h \
    vedittab.h \
    voutline.h \
    vsingleinstanceguard.h \
    vdirectory.h \
    vfile.h \
    vnotebookselector.h \
    vnofocusitemdelegate.h \
    vmdedit.h \
    dialog/vfindreplacedialog.h \
    dialog/vsettingsdialog.h \
    dialog/vdeletenotebookdialog.h \
    dialog/vselectdialog.h \
    vcaptain.h \
    vopenedlistmenu.h \
    vnavigationmode.h \
    vorphanfile.h \
    vcodeblockhighlighthelper.h \
    vwebview.h \
    vmdtab.h \
    vhtmltab.h \
    utils/vvim.h \
    utils/veditutils.h \
    vvimindicator.h \
    vbuttonwithwidget.h \
    vedittabinfo.h \
    vtabindicator.h \
    dialog/vupdater.h \
    dialog/vorphanfileinfodialog.h \
    vtextblockdata.h \
    utils/vpreviewutils.h \
    dialog/vconfirmdeletiondialog.h \
    vnotefile.h \
    vattachmentlist.h \
    dialog/vsortdialog.h \
    vfilesessioninfo.h \
    vtableofcontent.h \
    utils/vmetawordmanager.h \
    vmetawordlineedit.h \
    dialog/vinsertlinkdialog.h \
    vplaintextedit.h \
    vimageresourcemanager.h \
    vlinenumberarea.h \
    veditor.h \
    vmdeditor.h \
    veditconfig.h \
    vpreviewmanager.h \
    vimageresourcemanager2.h \
    vtextdocumentlayout.h \
    vtextedit.h \
    vsnippetlist.h \
    vsnippet.h \
    dialog/veditsnippetdialog.h \
    utils/vimnavigationforwidget.h \
    vtoolbox.h \
    vinsertselector.h \
    utils/vclipboardutils.h \
    vpalette.h \
    vbuttonmenuitem.h \
    utils/viconutils.h \
    lineeditdelegate.h \
    dialog/vtipsdialog.h \
    dialog/vcopytextashtmldialog.h \
    vwaitingwidget.h \
    utils/vwebutils.h \
    vlineedit.h \
    vcart.h \
    vvimcmdlineedit.h \
    vlistwidget.h \
    vsimplesearchinput.h \
    vstyleditemdelegate.h \
    vtreewidget.h \
    dialog/vexportdialog.h \
    vexporter.h \
    vwordcountinfo.h \
    vsearcher.h \
    vsearch.h \
    vsearchresulttree.h \
    isearchengine.h \
    vsearchconfig.h \
    vsearchengine.h \
    vuniversalentry.h \
    iuniversalentry.h \
    vlistwidgetdoublerows.h \
    vdoublerowitemwidget.h \
    vsearchue.h \
    voutlineue.h \
    vhelpue.h \
    vlistfolderue.h \
    dialog/vfixnotebookdialog.h \
    vplantumlhelper.h \
    vgraphvizhelper.h \
    vlivepreviewhelper.h \
    vmathjaxpreviewhelper.h \
    vmathjaxwebdocument.h \
    vmathjaxinplacepreviewhelper.h \
    markdownitoption.h \
    vhistorylist.h \
    vhistoryentry.h \
    vexplorer.h \
    vexplorerentry.h \
    vlistue.h \
    vuetitlecontentpanel.h \
    utils/vprocessutils.h \
    vtagpanel.h \
    valltagspanel.h \
    vtaglabel.h \
    vtagexplorer.h \
    markdownhighlighterdata.h \
    pegmarkdownhighlighter.h \
    pegparser.h \
    peghighlighterresult.h \
    vtexteditcompleter.h \
    vtextdocumentlayoutdata.h \
    utils/vkeyboardlayoutmanager.h \
    dialog/vkeyboardlayoutmappingdialog.h \
    vfilelistwidget.h \
    widgets/vcombobox.h \
    vtablehelper.h \
    vtable.h \
    dialog/vinserttabledialog.h

RESOURCES += \
    vnote.qrc \
    translations.qrc

macx {
    LIBS += -L/usr/local/lib
    INCLUDEPATH += /usr/local/include
}

INCLUDEPATH += $$PWD/../peg-highlight
DEPENDPATH += $$PWD/../peg-highlight

INCLUDEPATH += $$PWD/../hoedown
DEPENDPATH += $$PWD/../hoedown

win32-g++:CONFIG(release, debug|release) {
    LIBS += $$OUT_PWD/../peg-highlight/release/libpeg-highlight.a
    LIBS += $$OUT_PWD/../hoedown/release/libhoedown.a

    # Explicitly listing dependent static libraries.
    PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/release/libpeg-highlight.a
    PRE_TARGETDEPS += $$OUT_PWD/../hoedown/release/libhoedown.a
} else:win32-g++:CONFIG(debug, debug|release) {
    LIBS += $$OUT_PWD/../peg-highlight/debug/libpeg-highlight.a
    LIBS += $$OUT_PWD/../hoedown/debug/libhoedown.a

    PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/debug/libpeg-highlight.a
    PRE_TARGETDEPS += $$OUT_PWD/../hoedown/debug/libhoedown.a
} else:win32:!win32-g++:CONFIG(release, debug|release) {
    LIBS += $$OUT_PWD/../peg-highlight/release/peg-highlight.lib
    LIBS += $$OUT_PWD/../hoedown/release/hoedown.lib

    PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/release/peg-highlight.lib
    PRE_TARGETDEPS += $$OUT_PWD/../hoedown/release/hoedown.lib
} else:win32:!win32-g++:CONFIG(debug, debug|release) {
    LIBS += $$OUT_PWD/../peg-highlight/debug/peg-highlight.lib
    LIBS += $$OUT_PWD/../hoedown/debug/hoedown.lib

    PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/debug/peg-highlight.lib
    PRE_TARGETDEPS += $$OUT_PWD/../hoedown/debug/hoedown.lib
} else:unix {
    LIBS += $$OUT_PWD/../peg-highlight/libpeg-highlight.a
    LIBS += $$OUT_PWD/../hoedown/libhoedown.a

    PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/libpeg-highlight.a
    PRE_TARGETDEPS += $$OUT_PWD/../hoedown/libhoedown.a
}

## INSTALLS
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr
    DATADIR = $${PREFIX}/share

    # install desktop file
    desktop.path = $${DATADIR}/applications
    desktop.files += vnote.desktop

    # install icons
    icon16.path = $${DATADIR}/icons/hicolor/16x16/apps
    icon16.files = resources/icons/16x16/vnote.png

    icon32.path = $${DATADIR}/icons/hicolor/32x32/apps
    icon32.files = resources/icons/32x32/vnote.png

    icon48.path = $${DATADIR}/icons/hicolor/48x48/apps
    icon48.files = resources/icons/48x48/vnote.png

    icon64.path = $${DATADIR}/icons/hicolor/64x64/apps
    icon64.files = resources/icons/64x64/vnote.png

    icon128.path = $${DATADIR}/icons/hicolor/128x128/apps
    icon128.files = resources/icons/128x128/vnote.png

    icon256.path = $${DATADIR}/icons/hicolor/256x256/apps
    icon256.files = resources/icons/256x256/vnote.png

    iconsvg.path = $${DATADIR}/icons/hicolor/scalable/apps
    iconsvg.files = resources/icons/vnote.svg

    target.path = $${PREFIX}/bin

    lntarget.path = $${PREFIX}/bin
    lntarget.extra = $(SYMLINK) $(QMAKE_TARGET) $(INSTALL_ROOT)$${PREFIX}/bin/vnote

    INSTALLS += target lntarget desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg
    message("VNote will be installed in prefix $${PREFIX}")
}
