INCLUDEPATH *= $$PWD

include($$PWD/notebookbackend/notebookbackend.pri)

include($$PWD/versioncontroller/versioncontroller.pri)

include($$PWD/notebookconfigmgr/notebookconfigmgr.pri)

include($$PWD/notebook/notebook.pri)

include($$PWD/buffer/buffer.pri)

SOURCES += \
    $$PWD/buffermgr.cpp \
    $$PWD/configmgr.cpp \
    $$PWD/coreconfig.cpp \
    $$PWD/editorconfig.cpp \
    $$PWD/externalfile.cpp \
    $$PWD/file.cpp \
    $$PWD/htmltemplatehelper.cpp \
    $$PWD/logger.cpp \
    $$PWD/mainconfig.cpp \
    $$PWD/markdowneditorconfig.cpp \
    $$PWD/singleinstanceguard.cpp \
    $$PWD/texteditorconfig.cpp \
    $$PWD/vnotex.cpp \
    $$PWD/thememgr.cpp \
    $$PWD/notebookmgr.cpp \
    $$PWD/theme.cpp \
    $$PWD/sessionconfig.cpp \
    $$PWD/clipboarddata.cpp \
    $$PWD/widgetconfig.cpp

HEADERS += \
    $$PWD/buffermgr.h \
    $$PWD/configmgr.h \
    $$PWD/coreconfig.h \
    $$PWD/editorconfig.h \
    $$PWD/events.h \
    $$PWD/externalfile.h \
    $$PWD/file.h \
    $$PWD/filelocator.h \
    $$PWD/fileopenparameters.h \
    $$PWD/htmltemplatehelper.h \
    $$PWD/logger.h \
    $$PWD/mainconfig.h \
    $$PWD/markdowneditorconfig.h \
    $$PWD/singleinstanceguard.h \
    $$PWD/iconfig.h \
    $$PWD/texteditorconfig.h \
    $$PWD/vnotex.h \
    $$PWD/thememgr.h \
    $$PWD/global.h \
    $$PWD/namebasedserver.h \
    $$PWD/exception.h \
    $$PWD/notebookmgr.h \
    $$PWD/theme.h \
    $$PWD/sessionconfig.h \
    $$PWD/clipboarddata.h \
    $$PWD/webresource.h \
    $$PWD/widgetconfig.h
