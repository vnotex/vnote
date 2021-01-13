include($$PWD/../../common.pri)

TARGET = test_theme
TEMPLATE = app

SRC_FOLDER = $$PWD/../../../src
CORE_FOLDER = $$SRC_FOLDER/core
UTILS_FOLDER = $$SRC_FOLDER/utils

INCLUDEPATH *= $$SRC_FOLDER
INCLUDEPATH *= $$SRC_FOLDER/core

SOURCES += \
    test_theme.cpp \
    $$CORE_FOLDER/theme.cpp \
    $$UTILS_FOLDER/pathutils.cpp \
    $$UTILS_FOLDER/utils.cpp \
    $$UTILS_FOLDER/widgetutils.cpp \
    $$UTILS_FOLDER/fileutils.cpp \

HEADERS += \
    test_theme.h \
    $$CORE_FOLDER/exception.h \
    $$CORE_FOLDER/theme.h \
    $$UTILS_FOLDER/pathutils.h \
    $$UTILS_FOLDER/utils.h \
    $$UTILS_FOLDER/widgetutils.h \
    $$UTILS_FOLDER/fileutils.h \
