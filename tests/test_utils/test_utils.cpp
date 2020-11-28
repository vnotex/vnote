#include "test_utils.h"

#include <QDebug>
#include <QTemporaryDir>

#include <utils/pathutils.h>
#include <utils/fileutils.h>

using namespace tests;

using namespace vnotex;

void TestUtils::testParentDirPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("result");

    QTest::newRow("empty") << "" << "";

#if defined(Q_OS_WIN)
    QTest::newRow("win_root") << "c:\\" << "C:/";
    QTest::newRow("win_normal") << "c:\\users\\tamlok" << "C:/users";
    QTest::newRow("win_slash") << "c:\\users\\tamlok\\" << "C:/users/tamlok";
    QTest::newRow("win_ext") << "c:\\users\\tamlok\\vnotex.md" << "C:/users/tamlok";
    QTest::newRow("win_case") << "c:\\users\\Tamlok\\Vnotex.md" << "C:/users/Tamlok";
    QTest::newRow("win_sep") << "c:/users/tamlok/vnotex.md" << "C:/users/tamlok";
#else
    QTest::newRow("linux_root") << "/home" << "/";
    QTest::newRow("linux_normal") << "/home/tamlok" << "/home";
    QTest::newRow("linux_slash") << "/home/tamlok/" << "/home/tamlok";
    QTest::newRow("linux_ext") << "/home/tamlok/vnotex.md" << "/home/tamlok";
    QTest::newRow("linux_case") << "/home/Tamlok/vnotex.md" << "/home/Tamlok";
#endif
}

void TestUtils::testParentDirPath()
{
    QFETCH(QString, path);
    QFETCH(QString, result);

    QCOMPARE(PathUtils::parentDirPath(path), result);
}

void TestUtils::testCleanPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("result");

    QTest::newRow("empty") << "" << "";

#if defined(Q_OS_WIN)
    QTest::newRow("win_root") << "c:" << "c:";
    QTest::newRow("win_normal") << "c:\\users\\tamlok" << "c:/users/tamlok";
    QTest::newRow("win_slash") << "c:\\users\\tamlok\\" << "c:/users/tamlok";
    QTest::newRow("win_ext") << "C:\\users\\tamlok\\vnotex.md" << "C:/users/tamlok/vnotex.md";
    QTest::newRow("win_case") << "c:\\users\\Tamlok" << "c:/users/Tamlok";
    QTest::newRow("win_sep") << "c:/users/tamlok" << "c:/users/tamlok";
#else
    QTest::newRow("linux_root") << "/home" << "/home";
    QTest::newRow("linux_normal") << "/home/tamlok" << "/home/tamlok";
    QTest::newRow("linux_slash") << "/home/tamlok/" << "/home/tamlok";
    QTest::newRow("linux_ext") << "/home/tamlok/vnotex.md" << "/home/tamlok/vnotex.md";
    QTest::newRow("linux_case") << "/home/Tamlok/vnotex.md" << "/home/Tamlok/vnotex.md";
#endif
}

void TestUtils::testCleanPath()
{
    QFETCH(QString, path);
    QFETCH(QString, result);

    QCOMPARE(PathUtils::cleanPath(path), result);
}

void TestUtils::testAreSamePaths_data()
{
    QTest::addColumn<QString>("patha");
    QTest::addColumn<QString>("pathb");
    QTest::addColumn<bool>("result");

    QTest::newRow("empty") << "" << "" << true;

#if defined(Q_OS_WIN)
    QTest::newRow("win_normal") << "c:\\users\\tamlok" << "c:\\users\\tamlok" << true;
    QTest::newRow("win_slash") << "c:\\users\\vnotex\\" << "c:/users/vnotex" << true;
    QTest::newRow("win_file") << "C:\\users\\tamlok\\vnotex.md" << "C:/users/tamlok/vnotex.md" << true;
    QTest::newRow("win_file_false") << "C:\\users\\tamlok\\vnote.md" << "C:/users/tamlok/vnotex.md" << false;
    QTest::newRow("win_case") << "c:\\users\\Tamlok" << "C:/users/tamlok" << true;
#else
    QTest::newRow("linux_normal") << "/home/tamlok" << "/home/tamlok" << true;
    QTest::newRow("linux_slash") << "/home/tamlok/" << "/home/tamlok" << true;
    QTest::newRow("linux_file") << "/home/tamlok/vnotex.md" << "/home/tamlok/vnotex.md" << true;
    QTest::newRow("linux_file_false") << "/home/tamlok/vnote.md" << "/home/tamlok/vnotex.md" << false;
    QTest::newRow("linux_case") << "/home/Tamlok/vnotex.md" << "/home/tamlok/vnotex.md" << false;
#endif
}

void TestUtils::testAreSamePaths()
{
    QFETCH(QString, patha);
    QFETCH(QString, pathb);
    QFETCH(bool, result);

    QCOMPARE(PathUtils::areSamePaths(patha, pathb), result);
}

void TestUtils::testPathContains_data()
{
    QTest::addColumn<QString>("patha");
    QTest::addColumn<QString>("pathb");
    QTest::addColumn<bool>("result");

    QTest::newRow("empty") << "" << "" << true;

#if defined(Q_OS_WIN)
    QTest::newRow("win_same") << "c:\\users\\tamlok" << "c:\\users\\tamlok" << true;
    QTest::newRow("win_slash") << "c:\\users\\vnotex\\" << "c:\\users\\vnotex" << true;
    QTest::newRow("win_file") << "C:\\users\\tamlok" << "C:/users/tamlok/vnotex.md" << true;
    QTest::newRow("win_relative") << "C:\\users\\tamlok" << "tamlok/vnotex.md" << true;
    QTest::newRow("win_root") << "c:\\users\\Tamlok" << "D:/users/tamlok" << false;
    QTest::newRow("win_case") << "c:\\users\\Tamlok" << "c:/users/tamlok/abc" << true;
    QTest::newRow("win_parent") << "c:\\users\\Tamlok\\abc" << "c:/users/tamlok" << false;
    QTest::newRow("win_parents") << "c:\\users\\Tamlok\\abc\\def" << "c:/users/tamlok" << false;
#else
    QTest::newRow("linux_same") << "/home/tamlok" << "/home/tamlok" << true;
    QTest::newRow("linux_slash") << "/home/tamlok/" << "/home/tamlok" << true;
    QTest::newRow("linux_file") << "/home/tamlok" << "/home/tamlok/vnotex.md" << true;
    QTest::newRow("linux_relative") << "/home/tamlok" << "tamlok/vnotex.md" << true;
    QTest::newRow("linux_root") << "/home/tamlok" << "/tamlok/vnotex.md" << false;
    QTest::newRow("linux_case") << "/home/Tamlok" << "/home/tamlok/vnotex.md" << false;
    QTest::newRow("linux_parent") << "/home/tamlok/abc" << "/home/tamlok" << false;
    QTest::newRow("linux_parents") << "/home/tamlok/abc/def" << "/home/tamlok" << false;
#endif
}

void TestUtils::testPathContains()
{
    QFETCH(QString, patha);
    QFETCH(QString, pathb);
    QFETCH(bool, result);

    QCOMPARE(PathUtils::pathContains(patha, pathb), result);
}

void TestUtils::testConcatenateFilePath_data()
{
    QTest::addColumn<QString>("dirPath");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("result");

    QTest::newRow("emptyDir") << "" << "filea" << "filea";

#if defined(Q_OS_WIN)
    QTest::newRow("win_absolute") << "c:\\users\\tamlok" << "filea" << "c:/users/tamlok/filea";
    QTest::newRow("win_slash") << "c:\\users\\vnotex\\" << "filea" << "c:/users/vnotex/filea";
    QTest::newRow("win_slash2") << "C:/users/tamlok///" << "filea" << "C:/users/tamlok/filea";
    QTest::newRow("win_relative") << "users\\tamlok" << "filea" << "users/tamlok/filea";
    QTest::newRow("win_path") << "c:\\users\\tamlok" << "folder/filea" << "c:/users/tamlok/folder/filea";
#else
    QTest::newRow("linux_absolute") << "/home/tamlok" << "filea" << "/home/tamlok/filea";
    QTest::newRow("linux_slash") << "/home/tamlok///" << "filea" << "/home/tamlok/filea";
    QTest::newRow("linux_relative") << "home/tamlok" << "filea" << "home/tamlok/filea";
    QTest::newRow("linux_path") << "/home/tamloK" << "folder/filea" << "/home/tamloK/folder/filea";
#endif
}

void TestUtils::testConcatenateFilePath()
{
    QFETCH(QString, dirPath);
    QFETCH(QString, name);
    QFETCH(QString, result);

    QCOMPARE(PathUtils::concatenateFilePath(dirPath, name), result);
}

void TestUtils::testRenameFile()
{
    QTemporaryDir dir;
    const QString testFolderPath(dir.path());

    // File.
    {
        QString fileAPath = testFolderPath + "/filea.md";
        QFile fileA(fileAPath);
        QVERIFY(fileA.open(QIODevice::WriteOnly | QIODevice::Text));
        fileA.write(QByteArray());
        fileA.close();
        QVERIFY(QFileInfo::exists(fileAPath));
        FileUtils::renameFile(fileAPath, "fileb.md");
        QString newFileAPath = testFolderPath + "/fileb.md";
        QVERIFY(QFileInfo::exists(newFileAPath));
    }

    // Folder.
    {
        QDir paDir(testFolderPath);
        QString dirAPath = testFolderPath + "/dira";
        QVERIFY(paDir.mkdir("dira"));
        FileUtils::renameFile(dirAPath, "dirb");
        QVERIFY(paDir.exists("dirb"));
    }
}

void TestUtils::testIsText()
{
    QTemporaryDir dir;
    const QString testFolderPath(dir.path());

    {
        auto filePath = testFolderPath + "/text";
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(QByteArray(5, 'v'));
        file.close();
        QVERIFY(QFileInfo::exists(filePath));
        QVERIFY(FileUtils::isText(filePath));
    }

    {
        auto filePath = testFolderPath + "/bin";
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(reinterpret_cast<const char *>(&file), sizeof(file));
        file.close();
        QVERIFY(QFileInfo::exists(filePath));
        QVERIFY(!FileUtils::isText(filePath));
    }
}

QTEST_MAIN(tests::TestUtils)
