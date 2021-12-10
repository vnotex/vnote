include($$PWD/../../commonfull.pri)

TARGET = test_notebook
TEMPLATE = app

SOURCES += \
    dummynode.cpp \
    dummynotebook.cpp \
    test_notebook.cpp \
    testnotebookdatabase.cpp

HEADERS += \
    dummynode.h \
    dummynotebook.h \
    test_notebook.h \
    testnotebookdatabase.h
