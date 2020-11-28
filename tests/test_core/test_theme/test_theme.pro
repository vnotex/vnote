include($$PWD/../../common.pri)

TARGET = test_theme
TEMPLATE = app

SRC_FOLDER = $$PWD/../../../src
CORE_FOLDER = $$SRC_FOLDER/core
UTILS_FOLDER = $$SRC_FOLDER/utils

INCLUDEPATH *= $$SRC_FOLDER
INCLUDEPATH *= $$SRC_FOLDER/core

include($$UTILS_FOLDER/utils.pri)

SOURCES += \
    test_theme.cpp \
    $$CORE_FOLDER/theme.cpp \

HEADERS += \
    test_theme.h \
    $$CORE_FOLDER/exception.h \
    $$CORE_FOLDER/theme.h \
