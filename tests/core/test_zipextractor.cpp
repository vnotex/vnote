// Unit tests for ZipExtractor, the hardened miniz-backed ZIP reader used by the
// incremental updater.
//
// Covers step 7 of the client algorithm: "Archive validation before writing any
// bytes." Every rejection case must be caught by validate() so extract() leaves
// no partial tree behind.

#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopedPointer>
#include <QTemporaryDir>

#include <core/updatemanifest.h>
#include <core/zipextractor.h>

using namespace vnotex;

namespace tests {

namespace {

using EntryList = QVector<QPair<QString, QByteArray>>;

QByteArray blob(const QString &p_marker, int p_repeat = 1) {
  return p_marker.toUtf8().repeated(p_repeat);
}

QStringList sortedFileList(const QString &p_root) {
  QStringList out;
  QDirIterator it(p_root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    out.append(QDir(p_root).relativeFilePath(it.next()));
  }
  out.sort();
  return out;
}

QByteArray readFile(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return f.readAll();
}

// --------------------------------------------------------------------------
// Raw ZIP builder
// --------------------------------------------------------------------------
// miniz's WRITER validates entry names and refuses e.g. a leading '/', so it
// cannot produce the hostile archives the reader must defend against. A
// third-party producer (or an attacker) has no such scruples, so build those
// fixtures by hand: stored (uncompressed) entries, arbitrary names.

quint32 crc32Of(const QByteArray &p_data) {
  static quint32 table[256];
  static bool built = false;
  if (!built) {
    for (quint32 i = 0; i < 256; ++i) {
      quint32 c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
    built = true;
  }

  quint32 crc = 0xFFFFFFFFu;
  for (const char ch : p_data) {
    crc = table[(crc ^ static_cast<quint8>(ch)) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

void appendLE16(QByteArray *p_out, quint16 p_value) {
  p_out->append(static_cast<char>(p_value & 0xFF));
  p_out->append(static_cast<char>((p_value >> 8) & 0xFF));
}

void appendLE32(QByteArray *p_out, quint32 p_value) {
  for (int i = 0; i < 4; ++i) {
    p_out->append(static_cast<char>((p_value >> (8 * i)) & 0xFF));
  }
}

bool writeRawZip(const QString &p_path, const EntryList &p_entries) {
  QByteArray local;
  QByteArray central;

  for (const auto &entry : p_entries) {
    const QByteArray name = entry.first.toUtf8();
    const QByteArray data = entry.second;
    const quint32 crc = crc32Of(data);
    const quint32 offset = static_cast<quint32>(local.size());

    appendLE32(&local, 0x04034b50u); // local file header signature
    appendLE16(&local, 20);          // version needed
    appendLE16(&local, 0x0800);      // flags: UTF-8 name
    appendLE16(&local, 0);           // method: stored
    appendLE16(&local, 0);           // mod time
    appendLE16(&local, 0);           // mod date
    appendLE32(&local, crc);
    appendLE32(&local, static_cast<quint32>(data.size()));
    appendLE32(&local, static_cast<quint32>(data.size()));
    appendLE16(&local, static_cast<quint16>(name.size()));
    appendLE16(&local, 0); // extra length
    local.append(name);
    local.append(data);

    appendLE32(&central, 0x02014b50u); // central directory header signature
    appendLE16(&central, 20);          // version made by
    appendLE16(&central, 20);          // version needed
    appendLE16(&central, 0x0800);
    appendLE16(&central, 0);
    appendLE16(&central, 0);
    appendLE16(&central, 0);
    appendLE32(&central, crc);
    appendLE32(&central, static_cast<quint32>(data.size()));
    appendLE32(&central, static_cast<quint32>(data.size()));
    appendLE16(&central, static_cast<quint16>(name.size()));
    appendLE16(&central, 0); // extra
    appendLE16(&central, 0); // comment
    appendLE16(&central, 0); // disk number start
    appendLE16(&central, 0); // internal attributes
    appendLE32(&central, 0); // external attributes
    appendLE32(&central, offset);
    central.append(name);
  }

  QByteArray eocd;
  appendLE32(&eocd, 0x06054b50u);
  appendLE16(&eocd, 0);
  appendLE16(&eocd, 0);
  appendLE16(&eocd, static_cast<quint16>(p_entries.size()));
  appendLE16(&eocd, static_cast<quint16>(p_entries.size()));
  appendLE32(&eocd, static_cast<quint32>(central.size()));
  appendLE32(&eocd, static_cast<quint32>(local.size()));
  appendLE16(&eocd, 0);

  QFile file(p_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(local);
  file.write(central);
  file.write(eocd);
  const bool flushed = file.flush();
  file.close();
  return flushed;
}

} // namespace

class TestZipExtractor : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testRoundTrip();
  void testNestedDirectories();
  void testUnicodePaths();
  void testExplicitDirectoryEntries();
  void testRawZipFixtureIsReadable();

  void testMalformedArchive();
  void testMissingArchive();

  void testTraversalEntryRejected();
  void testTraversalEntryRejected_data();
  void testDuplicateEntriesRejected();
  void testCaseCollidingEntriesRejected();
  void testFileVsDirectoryConflictRejected();
  void testNestedUnderFileRejected();
  void testReservedNameRejected();
  void testStagingTargetedEntryRejected();

  void testTotalSizeCapBreach();
  void testEntryCountCapBreach();

  void testTopLevelDirStripping();
  void testTopLevelDirMissing();
  void testMultipleTopLevelDirs();

  void testExpectedEntrySetEquality();
  void testExpectedEntrySizeMismatch();

  void testNothingIsWrittenWhenValidationFails();
  void testNothingIsWrittenWhenAnEntryIsCorrupt();
  void testReadEntry();

private:
  QString archivePath(const QString &p_name) const {
    return m_dir->filePath(p_name);
  }
  QString destPath(const QString &p_name) const { return m_dir->filePath(p_name); }

  QScopedPointer<QTemporaryDir> m_dir;
};

void TestZipExtractor::init() {
  m_dir.reset(new QTemporaryDir());
  QVERIFY(m_dir->isValid());
}

void TestZipExtractor::cleanup() { m_dir.reset(); }

// ------------------------------------------------------------------ happy path

void TestZipExtractor::testRoundTrip() {
  const QString zip = archivePath(QStringLiteral("round.zip"));
  const EntryList entries{{QStringLiteral("vnote.exe"), blob(QStringLiteral("EXE"), 100)},
                          {QStringLiteral("readme.txt"), blob(QStringLiteral("hello"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, ZipExtractor::Options());
  QVERIFY2(result.isOk(), qPrintable(result.message));

  QCOMPARE(sortedFileList(dest),
           (QStringList{QStringLiteral("readme.txt"), QStringLiteral("vnote.exe")}));
  QCOMPARE(readFile(dest + QStringLiteral("/vnote.exe")), entries.at(0).second);
  QCOMPARE(readFile(dest + QStringLiteral("/readme.txt")), entries.at(1).second);

  QStringList extracted = result.extractedPaths;
  extracted.sort();
  QCOMPARE(extracted, (QStringList{QStringLiteral("readme.txt"), QStringLiteral("vnote.exe")}));
}

void TestZipExtractor::testNestedDirectories() {
  const QString zip = archivePath(QStringLiteral("nested.zip"));
  const EntryList entries{
      {QStringLiteral("platforms/qwindows.dll"), blob(QStringLiteral("PLAT"))},
      {QStringLiteral("resources/icons/deep/a/b/c.svg"), blob(QStringLiteral("SVG"))},
      {QStringLiteral("translations/vnote_zh_CN.qm"), blob(QStringLiteral("QM"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  const QString dest = destPath(QStringLiteral("out"));
  QVERIFY(ZipExtractor::extract(zip, dest, ZipExtractor::Options()).isOk());

  QVERIFY(QFileInfo::exists(dest + QStringLiteral("/platforms/qwindows.dll")));
  QVERIFY(QFileInfo::exists(dest + QStringLiteral("/resources/icons/deep/a/b/c.svg")));
  QVERIFY(QFileInfo::exists(dest + QStringLiteral("/translations/vnote_zh_CN.qm")));
}

void TestZipExtractor::testUnicodePaths() {
  const QString zip = archivePath(QStringLiteral("unicode.zip"));
  const QString path = QString::fromUtf8("资源/图标/日本語.png");
  const EntryList entries{{path, blob(QStringLiteral("PNG"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, ZipExtractor::Options());
  QVERIFY2(result.isOk(), qPrintable(result.message));
  QVERIFY(QFileInfo::exists(dest + QLatin1Char('/') + path));
  QCOMPARE(result.extractedPaths, QStringList{path});
}

void TestZipExtractor::testExplicitDirectoryEntries() {
  // Producers commonly emit explicit "dir/" entries. They must be created, and
  // must not be confused with files.
  const QString zip = archivePath(QStringLiteral("dirs.zip"));
  const EntryList entries{{QStringLiteral("a/b/file.txt"), blob(QStringLiteral("X"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries,
                                      QStringList{QStringLiteral("a"), QStringLiteral("a/b"),
                                                  QStringLiteral("empty")}));

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, ZipExtractor::Options());
  QVERIFY2(result.isOk(), qPrintable(result.message));
  QVERIFY(QFileInfo(dest + QStringLiteral("/empty")).isDir());
  QVERIFY(QFileInfo::exists(dest + QStringLiteral("/a/b/file.txt")));
  // Directories are not reported as extracted paths.
  QCOMPARE(result.extractedPaths, QStringList{QStringLiteral("a/b/file.txt")});
}

// Guards the hand-built fixtures used by the traversal cases: if writeRawZip()
// itself were broken, those archives would be rejected as CorruptArchive and
// the traversal assertions would pass for entirely the wrong reason.
void TestZipExtractor::testRawZipFixtureIsReadable() {
  const QString zip = archivePath(QStringLiteral("raw.zip"));
  const EntryList entries{{QStringLiteral("a.dll"), blob(QStringLiteral("AAAA"))},
                          {QStringLiteral("b/c.dll"), blob(QStringLiteral("BBBBBB"))}};
  QVERIFY(writeRawZip(zip, entries));

  QVector<ZipExtractor::Entry> parsed;
  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options(), &parsed);
  QVERIFY2(result.isOk(), qPrintable(result.message));
  QCOMPARE(parsed.size(), 2);

  const QString dest = destPath(QStringLiteral("rawout"));
  QVERIFY(ZipExtractor::extract(zip, dest, ZipExtractor::Options()).isOk());
  QCOMPARE(readFile(dest + QStringLiteral("/a.dll")), entries.at(0).second);
  QCOMPARE(readFile(dest + QStringLiteral("/b/c.dll")), entries.at(1).second);
}

// ------------------------------------------------------------------- rejection

void TestZipExtractor::testMalformedArchive() {
  const QString zip = archivePath(QStringLiteral("broken.zip"));
  QFile f(zip);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArrayLiteral("PK\x03\x04 this is not really a zip file at all"));
  f.close();

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QVERIFY(!result.isOk());
  QCOMPARE(result.status, ZipExtractor::Status::CorruptArchive);
}

void TestZipExtractor::testMissingArchive() {
  const auto result =
      ZipExtractor::validate(archivePath(QStringLiteral("nope.zip")), ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::OpenFailed);
}

void TestZipExtractor::testTraversalEntryRejected_data() {
  QTest::addColumn<QString>("entryPath");

  QTest::newRow("dotdot") << QStringLiteral("../evil.dll");
  QTest::newRow("dotdot-nested") << QStringLiteral("a/../../evil.dll");
  QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
  QTest::newRow("backslash-absolute") << QStringLiteral("\\windows\\evil.dll");
  QTest::newRow("drive") << QStringLiteral("C:/windows/evil.dll");
  QTest::newRow("unc") << QStringLiteral("//server/share/evil.dll");
  QTest::newRow("device") << QStringLiteral("\\\\?\\C:\\evil.dll");
  QTest::newRow("trailing-dot") << QStringLiteral("evil.");
  QTest::newRow("trailing-space") << QStringLiteral("evil ");
}

void TestZipExtractor::testTraversalEntryRejected() {
  QFETCH(QString, entryPath);

  const QString zip = archivePath(QStringLiteral("evil.zip"));
  // Hand-built: miniz's writer refuses some of these names outright, which is
  // exactly why the READER must not rely on the writer having been careful.
  QVERIFY(writeRawZip(zip, EntryList{{entryPath, blob(QStringLiteral("X"))}}));

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QVERIFY2(!result.isOk(), qPrintable(entryPath));
  QVERIFY2(result.status == ZipExtractor::Status::UnsafePath, qPrintable(result.message));
}

void TestZipExtractor::testDuplicateEntriesRejected() {
  const QString zip = archivePath(QStringLiteral("dup.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("a.dll"), blob(QStringLiteral("1"))},
                     {QStringLiteral("a.dll"), blob(QStringLiteral("2"))}}));

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::DuplicatePath);
}

void TestZipExtractor::testCaseCollidingEntriesRejected() {
  // Legal in a ZIP, catastrophic on a case-insensitive filesystem: the second
  // entry would silently overwrite the first.
  const QString zip = archivePath(QStringLiteral("case.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("resources/Icon.png"), blob(QStringLiteral("1"))},
                     {QStringLiteral("resources/icon.PNG"), blob(QStringLiteral("2"))}}));

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::DuplicatePath);
}

void TestZipExtractor::testFileVsDirectoryConflictRejected() {
  const QString zip = archivePath(QStringLiteral("conflict.zip"));
  QVERIFY(ZipExtractor::createArchive(zip,
                                      EntryList{{QStringLiteral("a"), blob(QStringLiteral("X"))}},
                                      QStringList{QStringLiteral("a")}));

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::PathTypeConflict);
}

void TestZipExtractor::testNestedUnderFileRejected() {
  // "a" is a file, but "a/b.dll" needs it to be a directory.
  const QString zip = archivePath(QStringLiteral("nestfile.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("a"), blob(QStringLiteral("X"))},
                     {QStringLiteral("a/b.dll"), blob(QStringLiteral("Y"))}}));

  const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::PathTypeConflict);
}

void TestZipExtractor::testReservedNameRejected() {
  for (const QString &name : {QStringLiteral("NUL"), QStringLiteral("a/COM1"),
                              QStringLiteral("aux.txt"), QStringLiteral("a/lpt9.dll")}) {
    const QString zip = archivePath(QStringLiteral("reserved.zip"));
    QFile::remove(zip);
    QVERIFY(ZipExtractor::createArchive(zip, EntryList{{name, blob(QStringLiteral("X"))}}));
    const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
    QVERIFY2(result.status == ZipExtractor::Status::UnsafePath, qPrintable(name));
  }
}

void TestZipExtractor::testStagingTargetedEntryRejected() {
  for (const QString &name : {QStringLiteral(".vnote-update/staged/a.dll"),
                              QStringLiteral(".vnote-old/1/a.dll"),
                              QStringLiteral(".VNOTE-UPDATE/pending.json")}) {
    const QString zip = archivePath(QStringLiteral("staging.zip"));
    QFile::remove(zip);
    QVERIFY(ZipExtractor::createArchive(zip, EntryList{{name, blob(QStringLiteral("X"))}}));
    const auto result = ZipExtractor::validate(zip, ZipExtractor::Options());
    QVERIFY2(result.status == ZipExtractor::Status::ReservedPath, qPrintable(name));
  }
}

void TestZipExtractor::testTotalSizeCapBreach() {
  const QString zip = archivePath(QStringLiteral("big.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("a.bin"), blob(QStringLiteral("0123456789"), 100)},
                     {QStringLiteral("b.bin"), blob(QStringLiteral("0123456789"), 100)}}));

  ZipExtractor::Options options;
  options.maxTotalUncompressedSize = 1500; // total is 2000
  const auto result = ZipExtractor::validate(zip, options);
  QCOMPARE(result.status, ZipExtractor::Status::SizeCapExceeded);

  options.maxTotalUncompressedSize = 2000;
  QVERIFY(ZipExtractor::validate(zip, options).isOk());
}

void TestZipExtractor::testEntryCountCapBreach() {
  const QString zip = archivePath(QStringLiteral("many.zip"));
  EntryList entries;
  for (int i = 0; i < 10; ++i) {
    entries.append({QStringLiteral("f%1.bin").arg(i), blob(QStringLiteral("X"))});
  }
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  ZipExtractor::Options options;
  options.maxEntries = 5;
  QCOMPARE(ZipExtractor::validate(zip, options).status, ZipExtractor::Status::SizeCapExceeded);
}

// -------------------------------------------------------- top-level directory

void TestZipExtractor::testTopLevelDirStripping() {
  // CPack wraps the full ZIP in "VNote-<ver>-<variant>/"; delta ZIPs do not.
  const QString zip = archivePath(QStringLiteral("full.zip"));
  const EntryList entries{
      {QStringLiteral("VNote-4.3.2-win64/vnote.exe"), blob(QStringLiteral("EXE"))},
      {QStringLiteral("VNote-4.3.2-win64/platforms/qwindows.dll"), blob(QStringLiteral("PLAT"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries,
                                      QStringList{QStringLiteral("VNote-4.3.2-win64")}));

  ZipExtractor::Options options;
  options.stripTopLevelDir = true;

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, options);
  QVERIFY2(result.isOk(), qPrintable(result.message));

  QCOMPARE(sortedFileList(dest), (QStringList{QStringLiteral("platforms/qwindows.dll"),
                                              QStringLiteral("vnote.exe")}));
  QVERIFY(!QFileInfo::exists(dest + QStringLiteral("/VNote-4.3.2-win64")));
}

void TestZipExtractor::testTopLevelDirMissing() {
  // A delta ZIP (root-relative entries) must be rejected when the caller asks
  // for stripping, rather than silently losing its first path component.
  const QString zip = archivePath(QStringLiteral("delta.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("vnote.exe"), blob(QStringLiteral("EXE"))}}));

  ZipExtractor::Options options;
  options.stripTopLevelDir = true;
  QCOMPARE(ZipExtractor::validate(zip, options).status,
           ZipExtractor::Status::TopLevelDirMismatch);

  // Without stripping it is perfectly valid.
  QVERIFY(ZipExtractor::validate(zip, ZipExtractor::Options()).isOk());
}

void TestZipExtractor::testMultipleTopLevelDirs() {
  const QString zip = archivePath(QStringLiteral("two.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("A/vnote.exe"), blob(QStringLiteral("1"))},
                     {QStringLiteral("B/other.dll"), blob(QStringLiteral("2"))}}));

  ZipExtractor::Options options;
  options.stripTopLevelDir = true;
  QCOMPARE(ZipExtractor::validate(zip, options).status,
           ZipExtractor::Status::TopLevelDirMismatch);
}

// ------------------------------------------------------- manifest cross-check

void TestZipExtractor::testExpectedEntrySetEquality() {
  const QString zip = archivePath(QStringLiteral("hop.zip"));
  const EntryList entries{{QStringLiteral("a.dll"), blob(QStringLiteral("AAAA"))},
                          {QStringLiteral("b/c.dll"), blob(QStringLiteral("BB"))}};
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  ZipExtractor::Options options;
  options.expectedEntries.insert(UpdateManifest::pathKey(QStringLiteral("a.dll")), 4);
  options.expectedEntries.insert(UpdateManifest::pathKey(QStringLiteral("b/c.dll")), 2);
  QVERIFY2(ZipExtractor::validate(zip, options).isOk(),
           qPrintable(ZipExtractor::validate(zip, options).message));

  // An entry the manifest did not expect.
  ZipExtractor::Options extra = options;
  extra.expectedEntries.remove(UpdateManifest::pathKey(QStringLiteral("b/c.dll")));
  QCOMPARE(ZipExtractor::validate(zip, extra).status, ZipExtractor::Status::EntrySetMismatch);

  // A manifest entry the archive is missing.
  ZipExtractor::Options missing = options;
  missing.expectedEntries.insert(UpdateManifest::pathKey(QStringLiteral("d.dll")), 1);
  QCOMPARE(ZipExtractor::validate(zip, missing).status, ZipExtractor::Status::EntrySetMismatch);
}

void TestZipExtractor::testExpectedEntrySizeMismatch() {
  const QString zip = archivePath(QStringLiteral("size.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("a.dll"), blob(QStringLiteral("AAAA"))}}));

  ZipExtractor::Options options;
  options.expectedEntries.insert(UpdateManifest::pathKey(QStringLiteral("a.dll")), 999);
  const auto result = ZipExtractor::validate(zip, options);
  QCOMPARE(result.status, ZipExtractor::Status::EntrySetMismatch);
  QVERIFY2(result.message.contains(QStringLiteral("999")), qPrintable(result.message));

  // -1 means "any size".
  options.expectedEntries.insert(UpdateManifest::pathKey(QStringLiteral("a.dll")), -1);
  QVERIFY(ZipExtractor::validate(zip, options).isOk());
}

// -------------------------------------------------------------- atomicity

void TestZipExtractor::testNothingIsWrittenWhenValidationFails() {
  // The bad entry is LAST, so a naive streaming extractor would already have
  // written the good ones by the time it noticed.
  const QString zip = archivePath(QStringLiteral("late.zip"));
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("good1.dll"), blob(QStringLiteral("1"))},
                     {QStringLiteral("good2.dll"), blob(QStringLiteral("2"))},
                     {QStringLiteral("../escape.dll"), blob(QStringLiteral("3"))}}));

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, ZipExtractor::Options());
  QCOMPARE(result.status, ZipExtractor::Status::UnsafePath);
  QVERIFY(result.extractedPaths.isEmpty());
  QVERIFY2(sortedFileList(dest).isEmpty(), qPrintable(sortedFileList(dest).join(',')));
  QVERIFY(!QFileInfo::exists(QFileInfo(dest).absolutePath() + QStringLiteral("/escape.dll")));
}

// The central directory can be perfectly well-formed while a COMPRESSED STREAM
// is corrupt. Because extract() writes entries one at a time, a bad byte in the
// LAST entry would otherwise be discovered only after every earlier entry had
// already landed in the destination -- breaking the "all or nothing" contract
// that the staging tree depends on.
void TestZipExtractor::testNothingIsWrittenWhenAnEntryIsCorrupt() {
  const QString zip = archivePath(QStringLiteral("corrupt.zip"));
  const EntryList entries{{QStringLiteral("good1.dll"), blob(QStringLiteral("1"), 200)},
                          {QStringLiteral("good2.dll"), blob(QStringLiteral("2"), 200)},
                          {QStringLiteral("bad.dll"), blob(QStringLiteral("3"), 200)}};
  QVERIFY(ZipExtractor::createArchive(zip, entries));

  // Corrupt the payload of the LAST entry by flipping bytes in the middle of
  // the file, well past the first two entries' data.
  QFile file(zip);
  QVERIFY(file.open(QIODevice::ReadWrite));
  QByteArray raw = file.readAll();
  QVERIFY(raw.size() > 200);
  // Walk backwards from just before the central directory to stay inside the
  // last entry's compressed data.
  const int centralDir = raw.lastIndexOf(QByteArrayLiteral("PK\x01\x02"));
  QVERIFY(centralDir > 40);
  for (int i = centralDir - 20; i < centralDir - 4; ++i) {
    raw[i] = static_cast<char>(raw[i] ^ 0xFF);
  }
  file.seek(0);
  file.write(raw);
  file.close();

  const QString dest = destPath(QStringLiteral("out"));
  const auto result = ZipExtractor::extract(zip, dest, ZipExtractor::Options());

  QVERIFY2(!result.isOk(), "a corrupt compressed stream must be rejected");
  QCOMPARE(result.status, ZipExtractor::Status::CorruptArchive);
  QVERIFY(result.extractedPaths.isEmpty());
  QVERIFY2(sortedFileList(dest).isEmpty(),
           qPrintable(QStringLiteral("destination is not empty: %1")
                          .arg(sortedFileList(dest).join(QLatin1Char(',')))));
}

void TestZipExtractor::testReadEntry() {
  const QString zip = archivePath(QStringLiteral("manifest.zip"));
  const QByteArray manifestBytes = QByteArrayLiteral("{\"schema\":1}");
  QVERIFY(ZipExtractor::createArchive(
      zip, EntryList{{QStringLiteral("manifest.json"), manifestBytes},
                     {QStringLiteral("vnote.exe"), blob(QStringLiteral("EXE"))}}));

  QByteArray out;
  QVERIFY(ZipExtractor::readEntry(zip, QStringLiteral("manifest.json"), &out));
  QCOMPARE(out, manifestBytes);

  // Case-insensitive lookup, matching Windows path semantics.
  out.clear();
  QVERIFY(ZipExtractor::readEntry(zip, QStringLiteral("Manifest.JSON"), &out));
  QCOMPARE(out, manifestBytes);

  QVERIFY(!ZipExtractor::readEntry(zip, QStringLiteral("absent.json"), &out));
  QVERIFY(!ZipExtractor::readEntry(zip, QStringLiteral("../evil"), &out));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestZipExtractor)
#include "test_zipextractor.moc"
