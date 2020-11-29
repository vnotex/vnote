TEMPLATE = subdirs

CONFIG += c++14

SUBDIRS = \
    libs \
    src \
    tests

src.depends = libs
tests.depends = libs
