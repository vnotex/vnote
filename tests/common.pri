lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5 and above")

equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 12): error("requires Qt 5.12 and above")

QT += core gui widgets network svg webenginewidgets webchannel
QT += testlib

CONFIG += c++14 testcase

CONFIG += no_testcase_installs
