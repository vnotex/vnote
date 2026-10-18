#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <core/services/filetypecoreservice.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

namespace {
QByteArray rawFileTypes(VxCoreContextHandle p_context) {
  char *json = nullptr;
  const auto err = vxcore_filetype_list(p_context, &json);
  const auto release = qScopeGuard([&]() { vxcore_string_free(json); });
  return err == VXCORE_OK && json ? QByteArray(json) : QByteArray();
}
} // namespace

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
  void testTypstRouting();
  void testSavedTypstAssociations_data();
  void testSavedTypstAssociations();
  void testDeletedTypstAssociation();
  void testImageFallbackPrecedence();
  void testImageRegistryProjection();
  void testSavedImageAssociations_data();
  void testSavedImageAssociations();

private:
  QTemporaryDir m_tempDir;
  QList<QPair<QByteArray, QByteArray>> m_tempEnvironment;
  VxCoreContextHandle m_context = nullptr;
  FileTypeCoreService *m_service = nullptr;
};

void TestFileTypeService::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  const auto tempRoot = m_tempDir.path().toUtf8();
  for (const char *name : {"TMPDIR", "TMP", "TEMP"}) {
    m_tempEnvironment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, tempRoot));
  }
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
  for (const auto &entry : m_tempEnvironment) {
    if (entry.second.isNull()) {
      qunsetenv(entry.first.constData());
    } else {
      qputenv(entry.first.constData(), entry.second);
    }
  }
  m_tempEnvironment.clear();
}

void TestFileTypeService::testConstructorWithNullContext() {
  FileTypeCoreService service(nullptr, QStringLiteral("en_US"));

  QCOMPARE(service.getAllFileTypes().size(), 0);
  QVERIFY(service.getFileTypeBySuffix(QStringLiteral("md")).m_typeName.isEmpty());
  QVERIFY(service.getFileTypeByName(QStringLiteral("Markdown")).m_typeName.isEmpty());
  QVERIFY(service.getFileType(QStringLiteral("test.md")).m_typeName.isEmpty());
  QVERIFY(service.getFileTypeByName(QStringLiteral("Image")).m_typeName.isEmpty());
  QVERIFY(service.getFileTypeBySuffix(QStringLiteral("png")).m_typeName.isEmpty());
  QVERIFY(service.getFileType(QStringLiteral("test.png")).m_typeName.isEmpty());
}

void TestFileTypeService::testConstructorWithValidContext() { QVERIFY(m_service != nullptr); }

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
  QVERIFY(type.m_isSearchable);
  QVERIFY(!m_service->getFileTypeByName(QStringLiteral("PDF")).m_isSearchable);
  QVERIFY(m_service->getFileTypeByName(QStringLiteral("MindMap")).m_isSearchable);
  QVERIFY(!m_service->getFileTypeByName(QStringLiteral("Others")).m_isSearchable);
}

void TestFileTypeService::testTypstRouting() {
  const auto beforeReads = rawFileTypes(m_context);
  QVERIFY(!beforeReads.isEmpty());
  const auto typst = m_service->getFileTypeByName(QStringLiteral("tYpSt"));
  QCOMPARE(typst.m_typeName, QStringLiteral("Typst"));
  QCOMPARE(typst.m_displayName, QStringLiteral("Typst"));
  QCOMPARE(typst.m_defaultDisplayName, QStringLiteral("Typst"));
  QCOMPARE(typst.m_suffixes, QStringList{QStringLiteral("typst")});
  QCOMPARE(typst.preferredSuffix(), QStringLiteral("typst"));
  QVERIFY(typst.m_isNewable);
  QVERIFY(typst.m_isSearchable);

  for (const auto &suffix : {QStringLiteral("typst"), QStringLiteral("TYPST")}) {
    const auto bySuffix = m_service->getFileTypeBySuffix(suffix);
    QCOMPARE(bySuffix.m_typeName, typst.m_typeName);
    QVERIFY(bySuffix.m_isNewable);
    QVERIFY(bySuffix.m_isSearchable);
    QCOMPARE(m_service->getFileType(QStringLiteral("notes/论文 draft.") + suffix).m_typeName,
             typst.m_typeName);
  }
  for (const auto &name :
       {QStringLiteral("Typst"), QStringLiteral("typst"), QStringLiteral("TYPST")}) {
    QCOMPARE(m_service->getFileTypeByName(name).m_typeName, typst.m_typeName);
  }
  QVERIFY(m_service->getFileTypeBySuffix(QStringLiteral("typ")).m_typeName != typst.m_typeName);
  QVERIFY(m_service->getFileTypeBySuffix(QStringLiteral("TYP")).m_typeName != typst.m_typeName);
  bool foundTypst = false;
  for (const auto &type : m_service->getAllFileTypes()) {
    if (type.m_typeName == typst.m_typeName) {
      QVERIFY(!foundTypst);
      foundTypst = true;
      QCOMPARE(type.preferredSuffix(), QStringLiteral("typst"));
      QVERIFY(type.m_isNewable);
      QVERIFY(type.m_isSearchable);
    }
  }
  QVERIFY(foundTypst);
  QCOMPARE(m_service->getFileType(QStringLiteral("image.PNG")).m_typeName, QStringLiteral("Image"));
  QCOMPARE(rawFileTypes(m_context), beforeReads);
}

void TestFileTypeService::testSavedTypstAssociations_data() {
  QTest::addColumn<QString>("name");
  QTest::addColumn<QStringList>("suffixes");
  QTest::addColumn<QString>("displayName");
  QTest::addColumn<bool>("newable");
  QTest::addColumn<bool>("searchable");

  QTest::newRow("builtin") << QStringLiteral("Typst") << QStringList{QStringLiteral("typst")}
                           << QStringLiteral("Typst") << true << true;
  QTest::newRow("custom-suffixes-before-image-fallback")
      << QStringLiteral("tYpSt") << QStringList{QStringLiteral("typeset"), QStringLiteral("pNg")}
      << QStringLiteral("My typesetting") << false << true;
  QTest::newRow("empty-suffixes-and-disabled-flags")
      << QStringLiteral("TyPsT") << QStringList() << QStringLiteral("No association") << false
      << false;
  QTest::newRow("explicit-search-exclusion")
      << QStringLiteral("Typst") << QStringList{QStringLiteral("typst")}
      << QStringLiteral("Private drafts") << true << false;
}

void TestFileTypeService::testSavedTypstAssociations() {
  QFETCH(QString, name);
  QFETCH(QStringList, suffixes);
  QFETCH(QString, displayName);
  QFETCH(bool, newable);
  QFETCH(bool, searchable);

  const auto original = rawFileTypes(m_context);
  QVERIFY(!original.isEmpty());
  const auto restore = qScopeGuard(
      [&]() { QCOMPARE(vxcore_filetype_set(m_context, original.constData()), VXCORE_OK); });
  const QJsonObject metadata{
      {QStringLiteral("displayName_en_US"), QStringLiteral("Localized typesetting")},
      {QStringLiteral("displayName_ja_JP"), QStringLiteral("組版ノート")},
      {QStringLiteral("custom"), QJsonObject{{QStringLiteral("enabled"), false},
                                             {QStringLiteral("order"), QJsonArray{3, 1, 2}}}}};
  const QJsonObject savedTypst{{QStringLiteral("name"), name},
                               {QStringLiteral("suffixes"), QJsonArray::fromStringList(suffixes)},
                               {QStringLiteral("displayName"), displayName},
                               {QStringLiteral("metadata"), metadata},
                               {QStringLiteral("isNewable"), newable},
                               {QStringLiteral("isSearchable"), searchable}};
  QJsonArray savedRows;
  const auto originalRows = QJsonDocument::fromJson(original).array();
  for (const auto &value : originalRows) {
    if (value.toObject()["name"].toString().compare(QLatin1String("Typst"), Qt::CaseInsensitive) ==
        0) {
      savedRows.append(savedTypst);
    } else {
      savedRows.append(value);
    }
  }
  const auto json = QJsonDocument(savedRows).toJson(QJsonDocument::Compact);
  QCOMPARE(vxcore_filetype_set(m_context, json.constData()), VXCORE_OK);
  const auto beforeReads = rawFileTypes(m_context);

  // Settings loads the actual wrapper rows and applies that list back to the registry.
  const auto rows = m_service->getAllFileTypes();
  bool foundTypst = false;
  for (const auto &type : rows) {
    if (type.m_typeName.compare(QLatin1String("Typst"), Qt::CaseInsensitive) == 0) {
      QVERIFY(!foundTypst);
      foundTypst = true;
      QCOMPARE(type.m_typeName, name);
      QCOMPARE(type.m_suffixes, suffixes);
      QCOMPARE(type.m_defaultDisplayName, displayName);
      QCOMPARE(type.m_displayName, metadata["displayName_en_US"].toString());
      QCOMPARE(type.m_metadata, metadata);
      QCOMPARE(type.m_isNewable, newable);
      QCOMPARE(type.m_isSearchable, searchable);
    }
  }
  QVERIFY(foundTypst);
  QCOMPARE(rawFileTypes(m_context), beforeReads);
  QVERIFY(m_service->setAllFileTypes(rows));
  const auto saved = rawFileTypes(m_context);
  QVERIFY(!saved.isEmpty());
  QJsonArray persistedWithoutImage;
  const auto persistedRows = QJsonDocument::fromJson(saved).array();
  for (const auto &value : persistedRows) {
    if (value.toObject()["name"].toString().compare(QLatin1String("Image"), Qt::CaseInsensitive) !=
        0) {
      persistedWithoutImage.append(value);
    }
  }
  QCOMPARE(persistedWithoutImage, savedRows);

  // A fresh backend context proves the save reached isolated config storage.
  VxCoreContextHandle reloadedContext = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &reloadedContext), VXCORE_OK);
  QVERIFY(reloadedContext != nullptr);
  const auto destroy = qScopeGuard([&]() { vxcore_context_destroy(reloadedContext); });
  FileTypeCoreService reloaded(reloadedContext, QStringLiteral("en_US"));
  QCOMPARE(rawFileTypes(reloadedContext), saved);
  const auto typst = reloaded.getFileTypeByName(QStringLiteral("TYPST"));
  QCOMPARE(typst.m_typeName, name);
  QCOMPARE(typst.m_suffixes, suffixes);
  QCOMPARE(typst.preferredSuffix(), suffixes.isEmpty() ? QString() : suffixes.first());
  QCOMPARE(typst.m_defaultDisplayName, displayName);
  QCOMPARE(typst.m_displayName, metadata["displayName_en_US"].toString());
  QCOMPARE(typst.m_metadata, metadata);
  QCOMPARE(typst.m_isNewable, newable);
  QCOMPARE(typst.m_isSearchable, searchable);
  for (const auto &suffix : suffixes) {
    const auto bySuffix = reloaded.getFileTypeBySuffix(suffix.toUpper());
    QCOMPARE(bySuffix.m_typeName, name);
    QCOMPARE(bySuffix.m_suffixes, suffixes);
    QCOMPARE(bySuffix.m_metadata, metadata);
    QCOMPARE(bySuffix.m_isNewable, newable);
    QCOMPARE(bySuffix.m_isSearchable, searchable);
    QCOMPARE(reloaded.getFileType(QStringLiteral("notes/document.") + suffix.toUpper()).m_typeName,
             name);
  }
  QCOMPARE(reloaded.getFileTypeBySuffix(QStringLiteral("typst")).m_typeName,
           suffixes.contains(QStringLiteral("typst"), Qt::CaseInsensitive)
               ? name
               : QStringLiteral("Others"));
  const auto image = reloaded.getFileTypeByName(QStringLiteral("Image"));
  QVERIFY(!image.m_isNewable);
  QVERIFY(!image.m_isSearchable);
  QCOMPARE(reloaded.getFileType(QStringLiteral("image.PNG")).m_typeName,
           suffixes.contains(QStringLiteral("png"), Qt::CaseInsensitive) ? name
                                                                         : QStringLiteral("Image"));
  QCOMPARE(reloaded.getFileType(QStringLiteral("image.JPG")).m_typeName, QStringLiteral("Image"));
  FileTypeCoreService otherLocale(reloadedContext, QStringLiteral("ja_JP"));
  QCOMPARE(otherLocale.getFileTypeByName(QStringLiteral("Typst")).m_displayName,
           metadata["displayName_ja_JP"].toString());
  QCOMPARE(rawFileTypes(reloadedContext), saved);
  QVERIFY(reloaded.setAllFileTypes(reloaded.getAllFileTypes()));
  QCOMPARE(rawFileTypes(reloadedContext), saved);
}

void TestFileTypeService::testDeletedTypstAssociation() {
  const auto original = rawFileTypes(m_context);
  QVERIFY(!original.isEmpty());
  const auto restore = qScopeGuard(
      [&]() { QCOMPARE(vxcore_filetype_set(m_context, original.constData()), VXCORE_OK); });
  auto rows = m_service->getAllFileTypes();
  bool removed = false;
  for (auto it = rows.begin(); it != rows.end();) {
    if (it->m_typeName.compare(QLatin1String("Typst"), Qt::CaseInsensitive) == 0) {
      it = rows.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  QVERIFY(removed);
  QVERIFY(m_service->setAllFileTypes(rows));
  const auto saved = rawFileTypes(m_context);
  VxCoreContextHandle reloadedContext = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &reloadedContext), VXCORE_OK);
  QVERIFY(reloadedContext != nullptr);
  const auto destroy = qScopeGuard([&]() { vxcore_context_destroy(reloadedContext); });
  FileTypeCoreService reloaded(reloadedContext, QStringLiteral("en_US"));
  QVERIFY(reloaded.getFileTypeByName(QStringLiteral("Typst")).m_typeName.isEmpty());
  QCOMPARE(reloaded.getFileTypeBySuffix(QStringLiteral("TYPST")).m_typeName,
           QStringLiteral("Others"));
  for (const auto &type : reloaded.getAllFileTypes()) {
    QVERIFY(type.m_typeName.compare(QLatin1String("Typst"), Qt::CaseInsensitive) != 0);
  }
  QCOMPARE(reloaded.getFileType(QStringLiteral("image.PNG")).m_typeName, QStringLiteral("Image"));
  QCOMPARE(rawFileTypes(reloadedContext), saved);
}

void TestFileTypeService::testImageFallbackPrecedence() {
  const auto original = rawFileTypes(m_context);
  QVERIFY(!original.isEmpty());
  const auto restore = qScopeGuard(
      [&]() { QCOMPARE(vxcore_filetype_set(m_context, original.constData()), VXCORE_OK); });

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto png = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ"
      "/pLvAAAAAElFTkSuQmCC");
  const auto path = dir.filePath(QStringLiteral("image.png"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(png), qint64(png.size()));
  file.close();
  const auto upperPath = dir.filePath(QStringLiteral("uppercase.PNG"));
  QVERIFY(QFile::copy(path, upperPath));

  for (const auto &candidate : {path, upperPath, QStringLiteral("notes/assets/image.png")}) {
    const auto type = m_service->getFileType(candidate);
    QCOMPARE(type.m_typeName, QStringLiteral("Image"));
    QVERIFY(!type.m_isNewable);
    QVERIFY(!type.m_isSearchable);
  }
  QCOMPARE(m_service->getFileType(QStringLiteral("diagram.svg")).m_typeName,
           QStringLiteral("Text"));
  QCOMPARE(m_service->getFileType(QStringLiteral("note.md")).m_typeName,
           QStringLiteral("Markdown"));
  QCOMPARE(m_service->getFileType(QStringLiteral("unknown.xyz123")).m_typeName,
           m_service->getFileTypeBySuffix(QStringLiteral("xyz123")).m_typeName);

  QCOMPARE(rawFileTypes(m_context), original);

  // A legacy registry has no Image entry and may already assign decoder suffixes to Text.
  QJsonArray overridden;
  const auto savedTypes = QJsonDocument::fromJson(original).array();
  for (const auto &value : savedTypes) {
    auto type = value.toObject();
    if (type["name"].toString().compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
      continue;
    }
    QJsonArray suffixes;
    const auto savedSuffixes = type["suffixes"].toArray();
    for (const auto &suffix : savedSuffixes) {
      if (suffix.toString().compare(QLatin1String("png"), Qt::CaseInsensitive) != 0 &&
          suffix.toString().compare(QLatin1String("svg"), Qt::CaseInsensitive) != 0) {
        suffixes.append(suffix);
      }
    }
    if (type["name"].toString() == QLatin1String("Text")) {
      suffixes.append(QStringLiteral("PNG"));
      suffixes.append(QStringLiteral("SvG"));
    }
    type["suffixes"] = suffixes;
    overridden.append(type);
  }
  const auto json = QJsonDocument(overridden).toJson(QJsonDocument::Compact);
  QCOMPARE(vxcore_filetype_set(m_context, json.constData()), VXCORE_OK);
  const auto legacy = rawFileTypes(m_context);
  QVERIFY(!legacy.isEmpty());
  const auto image = m_service->getFileTypeByName(QStringLiteral("Image"));
  QCOMPARE(image.m_typeName, QStringLiteral("Image"));
  QVERIFY(!image.m_suffixes.contains(QStringLiteral("png"), Qt::CaseInsensitive));
  QVERIFY(!image.m_suffixes.contains(QStringLiteral("svg"), Qt::CaseInsensitive));
  QCOMPARE(m_service->getFileTypeBySuffix(QStringLiteral("png")).m_typeName,
           QStringLiteral("Text"));
  QCOMPARE(m_service->getFileTypeBySuffix(QStringLiteral("SVG")).m_typeName,
           QStringLiteral("Text"));
  QCOMPARE(m_service->getFileType(path).m_typeName, QStringLiteral("Text"));
  QCOMPARE(m_service->getFileType(upperPath).m_typeName, QStringLiteral("Text"));
  QCOMPARE(m_service->getFileType(QStringLiteral("diagram.svg")).m_typeName,
           QStringLiteral("Text"));
  for (const auto &type : m_service->getAllFileTypes()) {
    if (type.m_typeName == QLatin1String("Image")) {
      QCOMPARE(type.m_suffixes, image.m_suffixes);
    }
  }
  QCOMPARE(rawFileTypes(m_context), legacy);
}

void TestFileTypeService::testImageRegistryProjection() {
  const auto original = rawFileTypes(m_context);
  QVERIFY(!original.isEmpty());
  const auto restore = qScopeGuard(
      [&]() { QCOMPARE(vxcore_filetype_set(m_context, original.constData()), VXCORE_OK); });

  QJsonArray legacyTypes;
  QStringList assignedSuffixes;
  const auto savedTypes = QJsonDocument::fromJson(original).array();
  for (const auto &value : savedTypes) {
    const auto type = value.toObject();
    if (type["name"].toString().compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
      continue;
    }
    legacyTypes.append(type);
    const auto suffixes = type["suffixes"].toArray();
    for (const auto &suffix : suffixes) {
      assignedSuffixes.append(suffix.toString());
    }
  }
  const auto json = QJsonDocument(legacyTypes).toJson(QJsonDocument::Compact);
  QCOMPARE(vxcore_filetype_set(m_context, json.constData()), VXCORE_OK);
  const auto beforeReads = rawFileTypes(m_context);
  QVERIFY(!beforeReads.isEmpty());

  FileTypeCoreService service(m_context, QStringLiteral("en_US"));
  const auto types = service.getAllFileTypes();
  const auto image = service.getFileTypeByName(QStringLiteral("image"));
  QCOMPARE(image.m_typeName, QStringLiteral("Image"));
  QCOMPARE(image.m_defaultDisplayName, QStringLiteral("Image"));
  QCOMPARE(image.m_displayName, FileTypeCoreService::tr("Image"));
  QVERIFY(!image.m_isNewable);
  QVERIFY(!image.m_isSearchable);
  QVERIFY(image.m_suffixes.contains(QStringLiteral("png")));
  auto normalized = image.m_suffixes;
  normalized.removeDuplicates();
  normalized.sort();
  QCOMPARE(image.m_suffixes, normalized);

  bool foundImage = false;
  for (const auto &type : types) {
    if (type.m_typeName == QLatin1String("Image")) {
      QVERIFY(!foundImage);
      foundImage = true;
      QCOMPARE(type.m_suffixes, image.m_suffixes);
      QCOMPARE(type.m_displayName, image.m_displayName);
      QVERIFY(!type.m_isNewable);
      QVERIFY(!type.m_isSearchable);
    }
  }
  QVERIFY(foundImage);

  // All installed format aliases participate, without a second extension table.
  const auto formats = QImageReader::supportedImageFormats();
  for (const auto &format : formats) {
    const auto suffix = QString::fromLatin1(format).trimmed().toLower();
    if (suffix.isEmpty()) {
      continue;
    }
    if (assignedSuffixes.contains(suffix, Qt::CaseInsensitive)) {
      QVERIFY(!image.m_suffixes.contains(suffix));
      continue;
    }
    QVERIFY(image.m_suffixes.contains(suffix));
    for (const auto &candidate : {suffix, suffix.toUpper()}) {
      const auto bySuffix = service.getFileTypeBySuffix(candidate);
      QCOMPARE(bySuffix.m_typeName, image.m_typeName);
      QCOMPARE(bySuffix.m_suffixes, image.m_suffixes);
      QCOMPARE(bySuffix.m_displayName, image.m_displayName);
      QVERIFY(!bySuffix.m_isNewable);
      QVERIFY(!bySuffix.m_isSearchable);
    }
  }
  QCOMPARE(service.getFileTypeBySuffix(QStringLiteral("svg")).m_typeName, QStringLiteral("Text"));
  QCOMPARE(service.getFileTypeBySuffix(QStringLiteral("md")).m_typeName,
           QStringLiteral("Markdown"));
  QCOMPARE(service.getFileTypeBySuffix(QStringLiteral("unrelatedsuffix123")).m_typeName,
           QStringLiteral("Others"));
  QCOMPARE(rawFileTypes(m_context), beforeReads);

  // The same list becomes persistent only through an explicit save (Settings Apply).
  QVERIFY(service.setAllFileTypes(types));
  const auto afterApply = rawFileTypes(m_context);
  QVERIFY(!afterApply.isEmpty());
  bool savedImage = false;
  const auto appliedTypes = QJsonDocument::fromJson(afterApply).array();
  for (const auto &value : appliedTypes) {
    const auto type = value.toObject();
    if (type["name"].toString() == QLatin1String("Image")) {
      savedImage = true;
      QCOMPARE(type["suffixes"].toArray(), QJsonArray::fromStringList(image.m_suffixes));
      QVERIFY(!type["isNewable"].toBool());
      QVERIFY(!type["isSearchable"].toBool());
    }
  }
  QVERIFY(savedImage);
  FileTypeCoreService reconstructed(m_context, QStringLiteral("en_US"));
  QCOMPARE(reconstructed.getFileTypeByName(QStringLiteral("Image")).m_suffixes, image.m_suffixes);
  QCOMPARE(rawFileTypes(m_context), afterApply);
}

void TestFileTypeService::testSavedImageAssociations_data() {
  QTest::addColumn<QString>("name");
  QTest::addColumn<QStringList>("suffixes");
  QTest::addColumn<QString>("displayName");
  QTest::addColumn<QJsonObject>("metadata");
  QTest::addColumn<QString>("expectedDisplayName");

  const QJsonObject customMetadata{{QStringLiteral("owner"), QStringLiteral("user")}};
  QTest::newRow("explicit-empty") << QStringLiteral("Image") << QStringList()
                                  << QStringLiteral("Image") << customMetadata
                                  << FileTypeCoreService::tr("Image");
  QTest::newRow("custom-suffixes-and-label")
      << QStringLiteral("Image")
      << QStringList{QStringLiteral("picturefile"), QStringLiteral("CustomImage")}
      << QStringLiteral("My Pictures") << customMetadata << QStringLiteral("My Pictures");
  auto localizedMetadata = customMetadata;
  localizedMetadata.insert(QStringLiteral("displayName_en_US"), QStringLiteral("Local pictures"));
  localizedMetadata.insert(QStringLiteral("displayName_ja_JP"), QStringLiteral("User translation"));
  QTest::newRow("case-insensitive-name-and-localized-label")
      << QStringLiteral("iMaGe") << QStringList() << QStringLiteral("Image") << localizedMetadata
      << QStringLiteral("Local pictures");
}

void TestFileTypeService::testSavedImageAssociations() {
  QFETCH(QString, name);
  QFETCH(QStringList, suffixes);
  QFETCH(QString, displayName);
  QFETCH(QJsonObject, metadata);
  QFETCH(QString, expectedDisplayName);

  const auto original = rawFileTypes(m_context);
  QVERIFY(!original.isEmpty());
  const auto restore = qScopeGuard(
      [&]() { QCOMPARE(vxcore_filetype_set(m_context, original.constData()), VXCORE_OK); });

  QJsonArray saved;
  const auto originalTypes = QJsonDocument::fromJson(original).array();
  for (const auto &value : originalTypes) {
    if (value.toObject()["name"].toString().compare(QLatin1String("Image"), Qt::CaseInsensitive) !=
        0) {
      saved.append(value);
    }
  }
  saved.append(QJsonObject{{QStringLiteral("name"), name},
                           {QStringLiteral("suffixes"), QJsonArray::fromStringList(suffixes)},
                           {QStringLiteral("displayName"), displayName},
                           {QStringLiteral("metadata"), metadata},
                           {QStringLiteral("isNewable"), true},
                           {QStringLiteral("isSearchable"), true}});
  const auto json = QJsonDocument(saved).toJson(QJsonDocument::Compact);
  QCOMPARE(vxcore_filetype_set(m_context, json.constData()), VXCORE_OK);
  const auto beforeReads = rawFileTypes(m_context);
  QVERIFY(!beforeReads.isEmpty());

  {
    FileTypeCoreService reconstructed(m_context, QStringLiteral("en_US"));
    auto types = reconstructed.getAllFileTypes();
    const auto image = reconstructed.getFileTypeByName(QStringLiteral("IMAGE"));
    QCOMPARE(image.m_typeName, name);
    QCOMPARE(image.m_suffixes, suffixes);
    QCOMPARE(image.m_defaultDisplayName, displayName);
    QCOMPARE(image.m_displayName, expectedDisplayName);
    QCOMPARE(image.m_metadata, metadata);
    QVERIFY(!image.m_isNewable);
    QVERIFY(!image.m_isSearchable);
    bool foundImage = false;
    for (auto &type : types) {
      if (type.m_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
        QVERIFY(!foundImage);
        foundImage = true;
        QCOMPARE(type.m_suffixes, suffixes);
        QCOMPARE(type.m_displayName, expectedDisplayName);
        QCOMPARE(type.m_metadata, metadata);
        QVERIFY(!type.m_isNewable);
        QVERIFY(!type.m_isSearchable);
        // Even an explicit save cannot make this viewer newable or searchable.
        type.m_isNewable = true;
        type.m_isSearchable = true;
      }
    }
    QVERIFY(foundImage);
    for (const auto &suffix : suffixes) {
      const auto bySuffix = reconstructed.getFileTypeBySuffix(suffix.toUpper());
      QCOMPARE(bySuffix.m_typeName, name);
      QCOMPARE(bySuffix.m_suffixes, suffixes);
      QCOMPARE(bySuffix.m_displayName, expectedDisplayName);
      QCOMPARE(bySuffix.m_metadata, metadata);
      QVERIFY(!bySuffix.m_isNewable);
      QVERIFY(!bySuffix.m_isSearchable);
    }
    QCOMPARE(reconstructed.getFileTypeBySuffix(QStringLiteral("png")).m_typeName,
             QStringLiteral("Others"));
    const auto inferred = reconstructed.getFileType(QStringLiteral("unassociated.png"));
    QCOMPARE(inferred.m_typeName, name);
    QCOMPARE(inferred.m_suffixes, suffixes);
    QCOMPARE(inferred.m_displayName, expectedDisplayName);
    QCOMPARE(inferred.m_metadata, metadata);
    QVERIFY(!inferred.m_isNewable);
    QVERIFY(!inferred.m_isSearchable);
    QCOMPARE(rawFileTypes(m_context), beforeReads);
    QVERIFY(reconstructed.setAllFileTypes(types));
  }

  const auto roundTripped = rawFileTypes(m_context);
  QVERIFY(!roundTripped.isEmpty());
  auto expected = QJsonDocument::fromJson(beforeReads).array();
  for (int i = 0; i < expected.size(); ++i) {
    auto type = expected.at(i).toObject();
    if (type["name"].toString().compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
      type["isNewable"] = false;
      type["isSearchable"] = false;
      expected[i] = type;
    }
  }
  QCOMPARE(QJsonDocument::fromJson(roundTripped).array(), expected);
  FileTypeCoreService reconstructed(m_context, QStringLiteral("en_US"));
  const auto image = reconstructed.getFileTypeByName(QStringLiteral("Image"));
  QCOMPARE(image.m_suffixes, suffixes);
  QCOMPARE(image.m_defaultDisplayName, displayName);
  QCOMPARE(image.m_displayName, expectedDisplayName);
  QCOMPARE(image.m_metadata, metadata);
  QVERIFY(!image.m_isNewable);
  QVERIFY(!image.m_isSearchable);
  QCOMPARE(reconstructed.getFileTypeBySuffix(QStringLiteral("PNG")).m_typeName,
           QStringLiteral("Others"));
  FileTypeCoreService otherLocale(m_context, QStringLiteral("ja_JP"));
  QCOMPARE(otherLocale.getFileTypeByName(QStringLiteral("Image")).m_displayName,
           metadata.contains(QStringLiteral("displayName_ja_JP"))
               ? metadata.value(QStringLiteral("displayName_ja_JP")).toString()
           : displayName == QLatin1String("Image") ? FileTypeCoreService::tr("Image")
                                                   : displayName);
  QCOMPARE(rawFileTypes(m_context), roundTripped);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileTypeService)
#include "test_filetypeservice.moc"
