#ifndef TEST_HELPER_H
#define TEST_HELPER_H

// Common test includes
#include <QDebug>
#include <QString>
#include <QtTest>

// QtKeychain completes on the native main queue on macOS. Use Cocoa's main
// dispatcher without changing the default UNIX dispatcher on worker threads.
// Other platforms retain the headless QCoreApplication test runner.
#ifdef Q_OS_MACOS
#define VNOTE_KEYCHAIN_TEST_MAIN(TestObject) QTEST_MAIN(TestObject)
#else
#define VNOTE_KEYCHAIN_TEST_MAIN(TestObject) QTEST_GUILESS_MAIN(TestObject)
#endif

// All test classes should be in the tests namespace
namespace tests {} // namespace tests

#endif // TEST_HELPER_H
