// test_error.cpp - Comprehensive tests for vnotex::Error class
#include <QtTest>

#include <core/error.h>

using namespace vnotex;

namespace tests {

class TestError : public QObject {
  Q_OBJECT

private slots:
  // Test default constructor
  void testDefaultConstructor();

  // Test parameterized constructor
  void testParameterizedConstructor();

  // Test static factory methods
  void testStaticOk();
  void testStaticError();

  // Test operator bool
  void testOperatorBool();

  // Test getters
  void testCodeGetter();
  void testMessageGetter();

  // Data-driven test for codeToString
  void testCodeToString_data();
  void testCodeToString();
};

void TestError::testDefaultConstructor() {
  Error err;
  QVERIFY(err.isOk());
  QCOMPARE(err.code(), ErrorCode::Ok);
  QVERIFY(err.message().isEmpty());
}

void TestError::testParameterizedConstructor() {
  Error err(ErrorCode::FailToReadFile, "test message");
  QVERIFY(!err.isOk());
  QCOMPARE(err.code(), ErrorCode::FailToReadFile);
  QCOMPARE(err.message(), QString("test message"));
}

void TestError::testStaticOk() {
  Error err = Error::ok();
  QVERIFY(err.isOk());
  QCOMPARE(err.code(), ErrorCode::Ok);
}

void TestError::testStaticError() {
  Error err = Error::error(ErrorCode::InvalidPath, "invalid");
  QVERIFY(!err.isOk());
  QCOMPARE(err.code(), ErrorCode::InvalidPath);
  QCOMPARE(err.message(), QString("invalid"));
}

void TestError::testOperatorBool() {
  // operator bool() returns true when there IS an error
  Error okErr = Error::ok();
  QVERIFY(!okErr); // Ok should be false (no error)

  if (okErr) {
    QFAIL("Error::ok() should not trigger if(error) block");
  }

  Error failErr = Error::error(ErrorCode::FailToWriteFile);
  QVERIFY(failErr); // Error should be true (has error)

  if (!failErr) {
    QFAIL("Error with code should trigger if(error) block");
  }
}

void TestError::testCodeGetter() {
  Error err(ErrorCode::FailToCopyDir);
  QCOMPARE(err.code(), ErrorCode::FailToCopyDir);
}

void TestError::testMessageGetter() {
  QString msg = "detailed error message";
  Error err(ErrorCode::InvalidArgument, msg);
  QCOMPARE(err.message(), msg);

  // Default message is empty
  Error errNoMsg(ErrorCode::InvalidPath);
  QVERIFY(errNoMsg.message().isEmpty());
}

void TestError::testCodeToString_data() {
  QTest::addColumn<ErrorCode>("code");
  QTest::addColumn<QString>("expected");

  QTest::newRow("Ok") << ErrorCode::Ok << "Ok";
  QTest::newRow("InvalidPath") << ErrorCode::InvalidPath << "InvalidPath";
  QTest::newRow("FailToCreateDir") << ErrorCode::FailToCreateDir << "FailToCreateDir";
  QTest::newRow("FailToWriteFile") << ErrorCode::FailToWriteFile << "FailToWriteFile";
  QTest::newRow("FailToReadFile") << ErrorCode::FailToReadFile << "FailToReadFile";
  QTest::newRow("FailToRenameFile") << ErrorCode::FailToRenameFile << "FailToRenameFile";
  QTest::newRow("FailToCopyFile") << ErrorCode::FailToCopyFile << "FailToCopyFile";
  QTest::newRow("FailToCopyDir") << ErrorCode::FailToCopyDir << "FailToCopyDir";
  QTest::newRow("FailToRemoveFile") << ErrorCode::FailToRemoveFile << "FailToRemoveFile";
  QTest::newRow("FailToRemoveDir") << ErrorCode::FailToRemoveDir << "FailToRemoveDir";
  QTest::newRow("FileMissingOnDisk") << ErrorCode::FileMissingOnDisk << "FileMissingOnDisk";
  QTest::newRow("FileExistsOnCreate") << ErrorCode::FileExistsOnCreate << "FileExistsOnCreate";
  QTest::newRow("DirExistsOnCreate") << ErrorCode::DirExistsOnCreate << "DirExistsOnCreate";
  QTest::newRow("InvalidArgument") << ErrorCode::InvalidArgument << "InvalidArgument";
}

void TestError::testCodeToString() {
  QFETCH(ErrorCode, code);
  QFETCH(QString, expected);

  QCOMPARE(Error::codeToString(code), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestError)
#include "test_error.moc"
