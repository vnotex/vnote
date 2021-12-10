#ifndef TESTS_UTILS_TEST_UTILS_H
#define TESTS_UTILS_TEST_UTILS_H

#include <QtTest>
#include <QJsonObject>

namespace tests
{
    class TestUtils : public QObject
    {
        Q_OBJECT
    public:
        explicit TestUtils(QObject *p_parent = nullptr);

    private slots:
        void initTestCase();

        // Define test cases here per slot.

        // Utils tests.
        void testParseAndReadJson_data();
        void testParseAndReadJson();

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

    private:
        QJsonObject m_obj;
    };
} // ns tests

#endif // TESTS_UTILS_TEST_UTILS_H
