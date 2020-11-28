include($$PWD/../common.pri)

TARGET = test_utils
TEMPLATE = app

SRC_FOLDER = $$PWD/../../src
UTILS_FOLDER = $$SRC_FOLDER/utils

INCLUDEPATH *= $$SRC_FOLDER

SOURCES += \
    test_utils.cpp \
    $$UTILS_FOLDER/pathutils.cpp \
    $$UTILS_FOLDER/fileutils.cpp

HEADERS += \
    test_utils.h \
    $$UTILS_FOLDER/pathutils.h \
    $$UTILS_FOLDER/fileutils.h
