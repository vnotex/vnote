#ifndef TESTS_UTILS_TEST_UTILS_H
#define TESTS_UTILS_TEST_UTILS_H

#include <QtTest>

namespace tests
{
    class TestUtils : public QObject
    {
        Q_OBJECT

    private slots:
        // Define test cases here per slot.

        // PathUtils Tests.
        void testParentDirPath_data();
        void testParentDirPath();

        void testCleanPath_data();
        void testCleanPath();

        void testAreSamePaths_data();
        void testAreSamePaths();

        void testPathContains_data();
        void testPathContains();

        void testConcatenateFilePath_data();
        void testConcatenateFilePath();

        // FileUtils Tests.
        void testRenameFile();

        void testIsText();
    };
} // ns tests

#endif // TESTS_UTILS_TEST_UTILS_H
