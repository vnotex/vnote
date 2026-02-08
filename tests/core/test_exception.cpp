// test_exception.cpp - Comprehensive tests for vnotex::Exception class
#include <QtTest>

#include <core/exception.h>

using namespace vnotex;

namespace tests {

class TestException : public QObject {
  Q_OBJECT

private slots:
  // Test constructor
  void testConstructor();

  // Test what() returns the message
  void testWhat();

  // Test m_type member
  void testTypeMember();

  // Test throwOne actually throws
  void testThrowOne();

  // Data-driven test for all exception types
  void testAllTypes_data();
  void testAllTypes();
};

void TestException::testConstructor() {
  Exception ex(Exception::Type::InvalidPath, "test path error");
  QCOMPARE(ex.m_type, Exception::Type::InvalidPath);
  QCOMPARE(QString::fromStdString(ex.what()), QString("test path error"));
}

void TestException::testWhat() {
  QString message = "detailed error description";
  Exception ex(Exception::Type::FailToWriteFile, message);
  QCOMPARE(QString::fromStdString(ex.what()), message);
}

void TestException::testTypeMember() {
  Exception ex(Exception::Type::FailToCopyDir, "copy failed");
  QCOMPARE(ex.m_type, Exception::Type::FailToCopyDir);
}

void TestException::testThrowOne() {
  // Suppress qCritical output during test
  QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(".*"));

  bool caught = false;
  QString expectedMessage = "test exception message";

  try {
    Exception::throwOne(Exception::Type::FileMissingOnDisk, expectedMessage);
    QFAIL("throwOne should have thrown an exception");
  } catch (const Exception &e) {
    caught = true;
    QCOMPARE(e.m_type, Exception::Type::FileMissingOnDisk);
    QCOMPARE(QString::fromStdString(e.what()), expectedMessage);
  } catch (...) {
    QFAIL("Caught unexpected exception type");
  }

  QVERIFY(caught);
}

void TestException::testAllTypes_data() {
  QTest::addColumn<Exception::Type>("type");
  QTest::addColumn<QString>("message");

  QTest::newRow("InvalidPath") << Exception::Type::InvalidPath << "invalid path";
  QTest::newRow("FailToCreateDir") << Exception::Type::FailToCreateDir << "create dir failed";
  QTest::newRow("FailToWriteFile") << Exception::Type::FailToWriteFile << "write failed";
  QTest::newRow("FailToReadFile") << Exception::Type::FailToReadFile << "read failed";
  QTest::newRow("FailToRenameFile") << Exception::Type::FailToRenameFile << "rename failed";
  QTest::newRow("FailToCopyFile") << Exception::Type::FailToCopyFile << "copy file failed";
  QTest::newRow("FailToCopyDir") << Exception::Type::FailToCopyDir << "copy dir failed";
  QTest::newRow("FailToRemoveFile") << Exception::Type::FailToRemoveFile << "remove file failed";
  QTest::newRow("FailToRemoveDir") << Exception::Type::FailToRemoveDir << "remove dir failed";
  QTest::newRow("FileMissingOnDisk") << Exception::Type::FileMissingOnDisk << "file missing";
  QTest::newRow("EssentialFileMissing") << Exception::Type::EssentialFileMissing << "essential missing";
  QTest::newRow("FileExistsOnCreate") << Exception::Type::FileExistsOnCreate << "file exists";
  QTest::newRow("DirExistsOnCreate") << Exception::Type::DirExistsOnCreate << "dir exists";
  QTest::newRow("InvalidArgument") << Exception::Type::InvalidArgument << "invalid arg";
}

void TestException::testAllTypes() {
  QFETCH(Exception::Type, type);
  QFETCH(QString, message);

  // Suppress qCritical output during test
  QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(".*"));

  bool caught = false;
  try {
    Exception::throwOne(type, message);
  } catch (const Exception &e) {
    caught = true;
    QCOMPARE(e.m_type, type);
    QCOMPARE(QString::fromStdString(e.what()), message);
  }

  QVERIFY2(caught, qPrintable(QString("Exception type %1 was not caught").arg(static_cast<int>(type))));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestException)
#include "test_exception.moc"
