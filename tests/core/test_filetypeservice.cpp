#include <QtTest>

#include <core/services/filetypecoreservice.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestFileTypeService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConstructorWithNullContext();
  void testConstructorWithValidContext();
  void testGetAllFileTypes();
  void testGetAllFileTypesContainExpectedTypes();
  void testGetFileTypeBySuffixMd();
  void testGetFileTypeBySuffixTxt();
  void testGetFileTypeBySuffixUnknown();
  void testGetFileTypeByNameMarkdown();
  void testGetFileTypeByNameUnknown();
  void testGetFileTypeByPath();
  void testFileTypeFields();

private:
  VxCoreContextHandle m_context = nullptr;
  FileTypeCoreService *m_service = nullptr;
};

void TestFileTypeService::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);

  m_service = new FileTypeCoreService(m_context, QStringLiteral("en_US"), this);
  QVERIFY(m_service != nullptr);
}

void TestFileTypeService::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestFileTypeService::testConstructorWithNullContext() {
  FileTypeCoreService service(nullptr, QStringLiteral("en_US"));

  QCOMPARE(service.getAllFileTypes().size(), 0);
  QVERIFY(service.getFileTypeBySuffix(QStringLiteral("md")).m_typeName.isEmpty());
  QVERIFY(service.getFileTypeByName(QStringLiteral("Markdown")).m_typeName.isEmpty());
  QVERIFY(service.getFileType(QStringLiteral("test.md")).m_typeName.isEmpty());
}

void TestFileTypeService::testConstructorWithValidContext() {
  QVERIFY(m_service != nullptr);
}

void TestFileTypeService::testGetAllFileTypes() {
  QVector<FileType> types = m_service->getAllFileTypes();
  QVERIFY(!types.isEmpty());

  bool hasMarkdown = false;
  for (const auto &type : types) {
    if (type.m_typeName == QStringLiteral("Markdown")) {
      hasMarkdown = true;
      break;
    }
  }
  QVERIFY(hasMarkdown);
}

void TestFileTypeService::testGetAllFileTypesContainExpectedTypes() {
  QVector<FileType> types = m_service->getAllFileTypes();

  bool hasMarkdown = false;
  bool hasText = false;
  bool hasOthers = false;

  for (const auto &type : types) {
    if (type.m_typeName == QStringLiteral("Markdown")) {
      hasMarkdown = true;
    } else if (type.m_typeName == QStringLiteral("Text")) {
      hasText = true;
    } else if (type.m_typeName == QStringLiteral("Others")) {
      hasOthers = true;
    }
  }

  QVERIFY(hasMarkdown);
  QVERIFY(hasText);
  QVERIFY(hasOthers);
}

void TestFileTypeService::testGetFileTypeBySuffixMd() {
  FileType type = m_service->getFileTypeBySuffix(QStringLiteral("md"));
  QCOMPARE(type.m_typeName, QStringLiteral("Markdown"));
}

void TestFileTypeService::testGetFileTypeBySuffixTxt() {
  FileType type = m_service->getFileTypeBySuffix(QStringLiteral("txt"));
  QVERIFY(!type.m_typeName.isEmpty());
  QVERIFY(type.m_typeName == QStringLiteral("Text") || type.m_typeName == QStringLiteral("Others"));
}

void TestFileTypeService::testGetFileTypeBySuffixUnknown() {
  FileType type = m_service->getFileTypeBySuffix(QStringLiteral("xyz123"));
  QVERIFY(type.m_typeName == QStringLiteral("Others") || type.m_typeName.isEmpty());
}

void TestFileTypeService::testGetFileTypeByNameMarkdown() {
  FileType type = m_service->getFileTypeByName(QStringLiteral("Markdown"));
  QCOMPARE(type.m_typeName, QStringLiteral("Markdown"));
  QVERIFY(type.m_suffixes.contains(QStringLiteral("md")));
}

void TestFileTypeService::testGetFileTypeByNameUnknown() {
  FileType type = m_service->getFileTypeByName(QStringLiteral("NonexistentType"));
  QVERIFY(type.m_typeName.isEmpty());
}

void TestFileTypeService::testGetFileTypeByPath() {
  FileType type = m_service->getFileType(QStringLiteral("test.md"));
  QCOMPARE(type.m_typeName, QStringLiteral("Markdown"));
}

void TestFileTypeService::testFileTypeFields() {
  FileType type = m_service->getFileTypeByName(QStringLiteral("Markdown"));
  QVERIFY(!type.m_typeName.isEmpty());
  QVERIFY(!type.m_displayName.isEmpty());
  QVERIFY(!type.m_suffixes.isEmpty());
  QVERIFY(type.m_isNewable);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileTypeService)
#include "test_filetypeservice.moc"
