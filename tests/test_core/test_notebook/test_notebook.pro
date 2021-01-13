include($$PWD/../../common.pri)

TARGET = test_notebook
TEMPLATE = app

SRC_FOLDER = $$PWD/../../../src
CORE_FOLDER = $$SRC_FOLDER/core

INCLUDEPATH *= $$SRC_FOLDER

LIBS_FOLDER = $$PWD/../../../libs

include($$LIBS_FOLDER/vtextedit/src/editor/editor_export.pri)

include($$LIBS_FOLDER/vtextedit/src/libs/syntax-highlighting/syntax-highlighting_export.pri)

include($$CORE_FOLDER/core.pri)
include($$SRC_FOLDER/widgets/widgets.pri)
include($$SRC_FOLDER/utils/utils.pri)
include($$SRC_FOLDER/export/export.pri)

SOURCES += \
    test_notebook.cpp

HEADERS += \
    test_notebook.h
