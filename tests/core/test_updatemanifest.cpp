// Unit tests for UpdateManifest, the pure value type behind the incremental
// updater. No widgets, no network, no filesystem.
//
// See .kilo/plans/1785337074532-incremental-update-plan.md § "Manifest format".

#include <QtTest>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/updatemanifest.h>

using namespace vnotex;

namespace tests {

namespace {

// A distinct, well-formed (64-char lowercase hex) digest per seed character.
// Using QString(64, seed) directly would produce non-hex digests for seeds like
// 'x' or 'z', which the parser correctly rejects.
QString fakeHash(char p_seed) {
  return QString::number(static_cast<uint>(static_cast<unsigned char>(p_seed)), 16)
      .rightJustified(64, QLatin1Char('0'));
}

QJsonObject fileEntry(const QString &p_path, qint64 p_size, const QString &p_sha) {
  QJsonObject o;
  o[QStringLiteral("path")] = p_path;
  o[QStringLiteral("size")] = static_cast<double>(p_size);
  o[QStringLiteral("sha256")] = p_sha;
  return o;
}

// A minimal well-formed manifest object. Callers mutate before parsing.
QJsonObject baseObject(const QString &p_version = QStringLiteral("4.3.1"),
                       const QString &p_channel = QStringLiteral("stable")) {
  QJsonObject o;
  o[QStringLiteral("schema")] = 1;
  o[QStringLiteral("product")] = QStringLiteral("VNote");
  o[QStringLiteral("channel")] = p_channel;
  o[QStringLiteral("version")] = p_version;
  o[QStringLiteral("variant")] = QStringLiteral("win64");
  o[QStringLiteral("platform")] = QStringLiteral("windows-x64");
  o[QStringLiteral("commit")] = QStringLiteral("deadbeef");
  o[QStringLiteral("generatedAt")] = QStringLiteral("2026-08-01T12:00:00Z");
  o[QStringLiteral("files")] = QJsonArray{fileEntry(QStringLiteral("vnote.exe"), 100, fakeHash('a'))};
  return o;
}

QJsonObject deltaBlock(const QString &p_baseVersion, qint64 p_size = 1000) {
  QJsonObject o;
  o[QStringLiteral("asset")] = QStringLiteral("VNote-x-win64.delta.zip");
  o[QStringLiteral("size")] = static_cast<double>(p_size);
  o[QStringLiteral("sha256")] = fakeHash('d');
  o[QStringLiteral("baseVersion")] = p_baseVersion;
  return o;
}

// Builds a stable manifest with the given files, and optionally a delta block.
UpdateManifest make(const QString &p_version, const QVector<QPair<QString, QString>> &p_files,
                    const QString &p_deltaBase = QString(), qint64 p_deltaSize = 1000,
                    const QString &p_channel = QStringLiteral("stable"),
                    qint64 p_fileSize = 100) {
  QJsonObject o = baseObject(p_version, p_channel);
  QJsonArray files;
  for (const auto &pair : p_files) {
    files.append(fileEntry(pair.first, p_fileSize, pair.second));
  }
  o[QStringLiteral("files")] = files;
  if (!p_deltaBase.isEmpty()) {
    o[QStringLiteral("delta")] = deltaBlock(p_deltaBase, p_deltaSize);
  }
  QString err;
  const auto m = UpdateManifest::fromJson(o, &err);
  Q_ASSERT_X(m.isValid(), "tests::make", qPrintable(err));
  return m;
}

} // namespace

class TestUpdateManifest : public QObject {
  Q_OBJECT

private slots:
  // Parsing.
  void testParseValid();
  void testParseMissingSchema();
  void testParseUnsupportedSchema();
  void testParseMissingRequiredFields();
  void testParseMalformedFiles();
  void testParseRejectsManifestJsonInFiles();
  void testParseRejectsDuplicateCaseInsensitivePaths();
  void testParseRejectsReservedDirectoryPaths();
  void testParseMalformedDeltaBlock();
  void testParseDeltaAbsent();
  void testRoundTrip();

  // Channel.
  void testNonStableChannelRejectedAsBase();

  // Diff.
  void testDiffAddChangeDelete();
  void testDiffCaseInsensitiveMatching();
  void testDeletionsDerived();

  // expectedChanged / hopArchiveSet.
  void testExpectedChangedRevertCase();
  void testExpectedChangedIntermediateOnlyCase();
  void testHopArchiveSetExcludesDeletions();

  // Chain.
  void testChainTerminates();
  void testChainAlreadyCurrent();
  void testChainBroken();
  void testChainMissingDelta();
  void testChainHopCap();
  void testChainSizeCap();
  void testChainRejectsNonStableIntermediate();
  void testChainRejectsVariantMismatch();
  void testChainRejectsCycle();

  // Base identity.
  void testValidateBaseIdentityMatches();
  void testValidateBaseIdentityVersionMismatch();
  void testValidateBaseIdentityCommitMismatch();
  void testValidateBaseIdentityFilesMismatch();

  // Path normalization.
  void testNormalizePathAccepts();
  void testNormalizePathRejects();
  void testNormalizePathRejects_data();
  void testPathKeyIsCaseFolded();
  void testVariantForBuild();
};

// ---------------------------------------------------------------- parsing

void TestUpdateManifest::testParseValid() {
  QJsonObject o = baseObject();
  o[QStringLiteral("files")] =
      QJsonArray{fileEntry(QStringLiteral("vnote.exe"), 5242880, fakeHash('a')),
                 fileEntry(QStringLiteral("platforms/qwindows.dll"), 1024, fakeHash('b'))};

  QString err;
  const auto m = UpdateManifest::fromJson(o, &err);
  QVERIFY2(m.isValid(), qPrintable(err));
  QCOMPARE(m.schema(), 1);
  QCOMPARE(m.product(), QStringLiteral("VNote"));
  QCOMPARE(m.channel(), QStringLiteral("stable"));
  QCOMPARE(m.version(), QStringLiteral("4.3.1"));
  QCOMPARE(m.variant(), QStringLiteral("win64"));
  QCOMPARE(m.platform(), QStringLiteral("windows-x64"));
  QCOMPARE(m.commit(), QStringLiteral("deadbeef"));
  QCOMPARE(m.files().size(), 2);
  QCOMPARE(m.totalExpandedSize(), Q_INT64_C(5243904));
  QVERIFY(m.isStableChannel());
  QVERIFY(!m.hasDelta());

  UpdateManifestFile f;
  QVERIFY(m.lookup(QStringLiteral("platforms/qwindows.dll"), &f));
  QCOMPARE(f.size, Q_INT64_C(1024));
  QVERIFY(!m.lookup(QStringLiteral("nope.dll")));
}

void TestUpdateManifest::testParseMissingSchema() {
  QJsonObject o = baseObject();
  o.remove(QStringLiteral("schema"));
  QString err;
  QVERIFY(!UpdateManifest::fromJson(o, &err).isValid());
  QVERIFY(err.contains(QStringLiteral("schema")));
}

void TestUpdateManifest::testParseUnsupportedSchema() {
  for (const int schema : {0, 2, 99, -1}) {
    QJsonObject o = baseObject();
    o[QStringLiteral("schema")] = schema;
    QVERIFY2(!UpdateManifest::fromJson(o).isValid(), qPrintable(QString::number(schema)));
  }
}

void TestUpdateManifest::testParseMissingRequiredFields() {
  const QStringList required{QStringLiteral("version"), QStringLiteral("variant"),
                             QStringLiteral("platform"), QStringLiteral("channel"),
                             QStringLiteral("files")};
  for (const QString &key : required) {
    QJsonObject o = baseObject();
    o.remove(key);
    QString err;
    QVERIFY2(!UpdateManifest::fromJson(o, &err).isValid(), qPrintable(key));
    QVERIFY2(err.contains(key), qPrintable(err));
  }
}

void TestUpdateManifest::testParseMalformedFiles() {
  // files is not an array.
  {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QStringLiteral("nope");
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // An entry is not an object.
  {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{QStringLiteral("vnote.exe")};
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Missing / non-numeric size.
  {
    QJsonObject entry = fileEntry(QStringLiteral("a.dll"), 1, fakeHash('a'));
    entry[QStringLiteral("size")] = QStringLiteral("100");
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{entry};
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Negative size.
  {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{fileEntry(QStringLiteral("a.dll"), -1, fakeHash('a'))};
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Bad hash: wrong length, non-hex, absent.
  for (const QString &bad :
       {QString(63, QLatin1Char('a')), QString(65, QLatin1Char('a')), QString(64, QLatin1Char('z')),
        QString()}) {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{fileEntry(QStringLiteral("a.dll"), 1, bad)};
    QVERIFY2(!UpdateManifest::fromJson(o).isValid(), qPrintable(bad));
  }
  // Unsafe path.
  {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] =
        QJsonArray{fileEntry(QStringLiteral("../evil.dll"), 1, fakeHash('a'))};
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Not even JSON.
  {
    QString err;
    QVERIFY(!UpdateManifest::fromJsonBytes(QByteArrayLiteral("{not json"), &err).isValid());
    QVERIFY(!err.isEmpty());
  }
  // Valid JSON, wrong root type.
  QVERIFY(!UpdateManifest::fromJsonBytes(QByteArrayLiteral("[1,2,3]")).isValid());
}

void TestUpdateManifest::testParseRejectsManifestJsonInFiles() {
  for (const QString &name : {QStringLiteral("manifest.json"), QStringLiteral("Manifest.JSON")}) {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{fileEntry(name, 10, fakeHash('a'))};
    QString err;
    QVERIFY2(!UpdateManifest::fromJson(o, &err).isValid(), qPrintable(name));
  }
}

void TestUpdateManifest::testParseRejectsDuplicateCaseInsensitivePaths() {
  QJsonObject o = baseObject();
  o[QStringLiteral("files")] =
      QJsonArray{fileEntry(QStringLiteral("resources/Icon.png"), 10, fakeHash('a')),
                 fileEntry(QStringLiteral("resources/icon.PNG"), 20, fakeHash('b'))};
  QString err;
  QVERIFY(!UpdateManifest::fromJson(o, &err).isValid());
  QVERIFY2(err.contains(QStringLiteral("duplicate")), qPrintable(err));
}

void TestUpdateManifest::testParseRejectsReservedDirectoryPaths() {
  for (const QString &p : {QStringLiteral(".vnote-update/staged/x.dll"),
                           QStringLiteral(".vnote-old/2026/x.dll"),
                           QStringLiteral(".VNOTE-UPDATE/x.dll")}) {
    QJsonObject o = baseObject();
    o[QStringLiteral("files")] = QJsonArray{fileEntry(p, 10, fakeHash('a'))};
    QVERIFY2(!UpdateManifest::fromJson(o).isValid(), qPrintable(p));
  }
}

void TestUpdateManifest::testParseMalformedDeltaBlock() {
  // Missing baseVersion.
  {
    QJsonObject d = deltaBlock(QStringLiteral("4.3.0"));
    d.remove(QStringLiteral("baseVersion"));
    QJsonObject o = baseObject();
    o[QStringLiteral("delta")] = d;
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // baseVersion == version is nonsensical and would build a self-loop.
  {
    QJsonObject o = baseObject(QStringLiteral("4.3.1"));
    o[QStringLiteral("delta")] = deltaBlock(QStringLiteral("4.3.1"));
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Asset name that is a path / traversal.
  {
    QJsonObject d = deltaBlock(QStringLiteral("4.3.0"));
    d[QStringLiteral("asset")] = QStringLiteral("../../etc/passwd");
    QJsonObject o = baseObject();
    o[QStringLiteral("delta")] = d;
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Zero size.
  {
    QJsonObject o = baseObject();
    o[QStringLiteral("delta")] = deltaBlock(QStringLiteral("4.3.0"), 0);
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
  // Malformed fullPackage.
  {
    QJsonObject fp;
    fp[QStringLiteral("asset")] = QStringLiteral("VNote.zip");
    QJsonObject o = baseObject();
    o[QStringLiteral("fullPackage")] = fp;
    QVERIFY(!UpdateManifest::fromJson(o).isValid());
  }
}

void TestUpdateManifest::testParseDeltaAbsent() {
  // The first release shipping this feature has no delta block at all; that is
  // a valid manifest, it just disables the delta path.
  const auto m = UpdateManifest::fromJson(baseObject());
  QVERIFY(m.isValid());
  QVERIFY(!m.hasDelta());
  QVERIFY(!m.delta().isValid());
  QVERIFY(!m.toJson().contains(QStringLiteral("delta")));
}

void TestUpdateManifest::testRoundTrip() {
  QJsonObject o = baseObject();
  o[QStringLiteral("files")] =
      QJsonArray{fileEntry(QStringLiteral("vnote.exe"), 5242880, fakeHash('a')),
                 fileEntry(QStringLiteral("translations/vnote_zh_CN.qm"), 4096, fakeHash('b'))};
  o[QStringLiteral("delta")] = deltaBlock(QStringLiteral("4.3.0"), 9400000);
  QJsonObject fp;
  fp[QStringLiteral("asset")] = QStringLiteral("VNote-4.3.1-win64.zip");
  fp[QStringLiteral("size")] = 172000000.0;
  fp[QStringLiteral("sha256")] = fakeHash('c');
  o[QStringLiteral("fullPackage")] = fp;

  QString err;
  const auto first = UpdateManifest::fromJson(o, &err);
  QVERIFY2(first.isValid(), qPrintable(err));

  const auto second = UpdateManifest::fromJson(first.toJson(), &err);
  QVERIFY2(second.isValid(), qPrintable(err));

  QCOMPARE(second.version(), first.version());
  QCOMPARE(second.commit(), first.commit());
  QCOMPARE(second.files().size(), first.files().size());
  QCOMPARE(second.totalExpandedSize(), first.totalExpandedSize());
  QCOMPARE(second.delta().baseVersion, QStringLiteral("4.3.0"));
  QCOMPARE(second.delta().size, Q_INT64_C(9400000));
  QCOMPARE(second.fullPackage().asset, QStringLiteral("VNote-4.3.1-win64.zip"));
  QCOMPARE(QJsonDocument(second.toJson()).toJson(QJsonDocument::Compact),
           QJsonDocument(first.toJson()).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------- channel

void TestUpdateManifest::testNonStableChannelRejectedAsBase() {
  // A continuous manifest parses fine (we must be able to READ one), but it is
  // never eligible as a delta base.
  const auto cont = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}},
                         QString(), 1000, QStringLiteral("continuous"));
  QVERIFY(cont.isValid());
  QVERIFY(!cont.isStableChannel());

  const auto stable = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
  QString err;
  QVERIFY(!UpdateManifest::validateBaseIdentity(cont, stable, &err));
  QVERIFY2(err.contains(QStringLiteral("channel")), qPrintable(err));

  // And as the newest manifest in a chain walk.
  const auto newestCont =
      make(QStringLiteral("4.3.2"), {{QStringLiteral("a.dll"), fakeHash('b')}},
           QStringLiteral("4.3.1"), 1000, QStringLiteral("continuous"));
  QHash<QString, UpdateManifest> available;
  available.insert(newestCont.version(), newestCont);
  available.insert(stable.version(), stable);
  const auto chain =
      UpdateManifest::resolveChain(newestCont, QStringLiteral("4.3.1"), available);
  QCOMPARE(chain.status, UpdateManifest::ChainStatus::NonStableBase);
}

// ------------------------------------------------------------------- diff

void TestUpdateManifest::testDiffAddChangeDelete() {
  const auto base = make(QStringLiteral("4.3.0"), {{QStringLiteral("same.dll"), fakeHash('a')},
                                                   {QStringLiteral("changed.dll"), fakeHash('b')},
                                                   {QStringLiteral("gone.dll"), fakeHash('c')}});
  const auto target = make(QStringLiteral("4.3.1"), {{QStringLiteral("same.dll"), fakeHash('a')},
                                                     {QStringLiteral("changed.dll"), fakeHash('d')},
                                                     {QStringLiteral("new.dll"), fakeHash('e')}});

  const auto d = UpdateManifest::diff(base, target);
  QCOMPARE(d.added, QStringList{QStringLiteral("new.dll")});
  QCOMPARE(d.changed, QStringList{QStringLiteral("changed.dll")});
  QCOMPARE(d.removed, QStringList{QStringLiteral("gone.dll")});
  QVERIFY(!d.isEmpty());

  // Identical manifests produce an empty diff.
  QVERIFY(UpdateManifest::diff(base, base).isEmpty());
}

void TestUpdateManifest::testDiffCaseInsensitiveMatching() {
  // Same file, different casing between releases: NOT a change, and NOT a
  // delete+add pair. Windows would treat both names as the same file.
  const auto base = make(QStringLiteral("4.3.0"), {{QStringLiteral("Resources/Icon.png"), fakeHash('a')}});
  const auto target = make(QStringLiteral("4.3.1"), {{QStringLiteral("resources/icon.png"), fakeHash('a')}});

  const auto d = UpdateManifest::diff(base, target);
  QVERIFY2(d.isEmpty(), qPrintable(QStringLiteral("added=%1 changed=%2 removed=%3")
                                       .arg(d.added.join(QLatin1Char(',')),
                                            d.changed.join(QLatin1Char(',')),
                                            d.removed.join(QLatin1Char(',')))));

  // Hash comparison is also case-insensitive (hex digests).
  const auto upper =
      make(QStringLiteral("4.3.1"), {{QStringLiteral("resources/icon.png"), fakeHash('a').toUpper()}});
  QVERIFY(UpdateManifest::diff(base, upper).isEmpty());
}

void TestUpdateManifest::testDeletionsDerived() {
  const auto base = make(QStringLiteral("4.3.0"), {{QStringLiteral("keep.dll"), fakeHash('a')},
                                                   {QStringLiteral("old1.dll"), fakeHash('b')},
                                                   {QStringLiteral("old2.dll"), fakeHash('c')}});
  const auto target = make(QStringLiteral("4.3.1"), {{QStringLiteral("keep.dll"), fakeHash('a')}});

  const QStringList del = UpdateManifest::deletions(base, target);
  QCOMPARE(del, (QStringList{QStringLiteral("old1.dll"), QStringLiteral("old2.dll")}));

  // Deletions are never in the archive set.
  QVERIFY(UpdateManifest::hopArchiveSet(base, target).isEmpty());
}

// -------------------------------------------------------- expectedChanged

// A:H1 -> B:H2 -> C:H1. The file was touched by hop B and reverted by hop C, so
// relative to the VERIFIED base (A) and the FINAL target (C) it did not change
// and must NOT be staged.
void TestUpdateManifest::testExpectedChangedRevertCase() {
  const auto a = make(QStringLiteral("4.3.0"), {{QStringLiteral("flip.dll"), fakeHash('1')},
                                                {QStringLiteral("other.dll"), fakeHash('x')}});
  const auto b = make(QStringLiteral("4.3.1"), {{QStringLiteral("flip.dll"), fakeHash('2')},
                                                {QStringLiteral("other.dll"), fakeHash('x')}});
  const auto c = make(QStringLiteral("4.3.2"), {{QStringLiteral("flip.dll"), fakeHash('1')},
                                                {QStringLiteral("other.dll"), fakeHash('y')}});

  // Each individual hop DOES carry flip.dll...
  QVERIFY(UpdateManifest::hopArchiveSet(a, b).contains(QStringLiteral("flip.dll")));
  QVERIFY(UpdateManifest::hopArchiveSet(b, c).contains(QStringLiteral("flip.dll")));

  // ...but the end-to-end expectation does not.
  const QStringList expected = UpdateManifest::expectedChanged(a, c);
  QCOMPARE(expected, QStringList{QStringLiteral("other.dll")});
  QVERIFY(!expected.contains(QStringLiteral("flip.dll")));
}

// A file that exists only in an intermediate release (added by B, deleted by C)
// must not appear in the end-to-end expectation either.
void TestUpdateManifest::testExpectedChangedIntermediateOnlyCase() {
  const auto a = make(QStringLiteral("4.3.0"), {{QStringLiteral("keep.dll"), fakeHash('a')}});
  const auto b = make(QStringLiteral("4.3.1"), {{QStringLiteral("keep.dll"), fakeHash('a')},
                                                {QStringLiteral("temp.dll"), fakeHash('t')}});
  const auto c = make(QStringLiteral("4.3.2"), {{QStringLiteral("keep.dll"), fakeHash('z')}});

  QVERIFY(UpdateManifest::hopArchiveSet(a, b).contains(QStringLiteral("temp.dll")));

  const QStringList expected = UpdateManifest::expectedChanged(a, c);
  QCOMPARE(expected, QStringList{QStringLiteral("keep.dll")});
  QVERIFY(!expected.contains(QStringLiteral("temp.dll")));

  // temp.dll is not a deletion relative to A either -- it never existed there.
  QVERIFY(UpdateManifest::deletions(a, c).isEmpty());
}

void TestUpdateManifest::testHopArchiveSetExcludesDeletions() {
  const auto base = make(QStringLiteral("4.3.0"), {{QStringLiteral("keep.dll"), fakeHash('a')},
                                                   {QStringLiteral("drop.dll"), fakeHash('b')}});
  const auto target = make(QStringLiteral("4.3.1"), {{QStringLiteral("keep.dll"), fakeHash('c')},
                                                     {QStringLiteral("add.dll"), fakeHash('d')}});

  const QStringList archive = UpdateManifest::hopArchiveSet(base, target);
  QCOMPARE(archive, (QStringList{QStringLiteral("add.dll"), QStringLiteral("keep.dll")}));
  QVERIFY(!archive.contains(QStringLiteral("drop.dll")));
  QCOMPARE(UpdateManifest::deletions(base, target), QStringList{QStringLiteral("drop.dll")});
}

// ------------------------------------------------------------------ chain

void TestUpdateManifest::testChainTerminates() {
  // 4.3.0 -> 4.3.1 -> 4.3.2, installed 4.3.0.
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};
  const auto v0 = make(QStringLiteral("4.3.0"), files, QString(), 0, QStringLiteral("stable"),
                       10 * 1000 * 1000);
  const auto v1 = make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.0"), 1000,
                       QStringLiteral("stable"), 10 * 1000 * 1000);
  const auto v2 = make(QStringLiteral("4.3.2"), files, QStringLiteral("4.3.1"), 2000,
                       QStringLiteral("stable"), 10 * 1000 * 1000);

  QHash<QString, UpdateManifest> available;
  available.insert(v0.version(), v0);
  available.insert(v1.version(), v1);
  available.insert(v2.version(), v2);

  const auto r = UpdateManifest::resolveChain(v2, QStringLiteral("4.3.0"), available);
  QCOMPARE(r.status, UpdateManifest::ChainStatus::Ok);
  QVERIFY(r.isOk());
  // Oldest hop first.
  QCOMPARE(r.hopVersions, (QStringList{QStringLiteral("4.3.1"), QStringLiteral("4.3.2")}));
  QCOMPARE(r.totalDeltaSize, Q_INT64_C(3000));

  // Single-hop case.
  const auto single = UpdateManifest::resolveChain(v2, QStringLiteral("4.3.1"), available);
  QCOMPARE(single.status, UpdateManifest::ChainStatus::Ok);
  QCOMPARE(single.hopVersions, QStringList{QStringLiteral("4.3.2")});
  QCOMPARE(single.totalDeltaSize, Q_INT64_C(2000));
}

void TestUpdateManifest::testChainAlreadyCurrent() {
  const auto v1 = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}},
                       QStringLiteral("4.3.0"));
  QHash<QString, UpdateManifest> available;
  available.insert(v1.version(), v1);
  const auto r = UpdateManifest::resolveChain(v1, QStringLiteral("4.3.1"), available);
  QCOMPARE(r.status, UpdateManifest::ChainStatus::AlreadyCurrent);
  QVERIFY(r.hopVersions.isEmpty());
}

void TestUpdateManifest::testChainBroken() {
  // 4.3.2 declares a base of 4.3.1, but 4.3.1's manifest is unavailable.
  const auto v2 = make(QStringLiteral("4.3.2"), {{QStringLiteral("a.dll"), fakeHash('a')}},
                       QStringLiteral("4.3.1"));
  QHash<QString, UpdateManifest> available;
  available.insert(v2.version(), v2);

  const auto r = UpdateManifest::resolveChain(v2, QStringLiteral("4.3.0"), available);
  QCOMPARE(r.status, UpdateManifest::ChainStatus::BrokenChain);
  QVERIFY(r.hopVersions.isEmpty());
}

void TestUpdateManifest::testChainMissingDelta() {
  // The newest manifest has no delta block at all.
  const auto v2 = make(QStringLiteral("4.3.2"), {{QStringLiteral("a.dll"), fakeHash('a')}});
  QHash<QString, UpdateManifest> available;
  available.insert(v2.version(), v2);
  const auto r = UpdateManifest::resolveChain(v2, QStringLiteral("4.3.0"), available);
  QCOMPARE(r.status, UpdateManifest::ChainStatus::MissingDelta);

  // An INTERMEDIATE hop without a delta block is equally fatal.
  const auto v1 = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
  const auto v2d = make(QStringLiteral("4.3.2"), {{QStringLiteral("a.dll"), fakeHash('b')}},
                        QStringLiteral("4.3.1"));
  QHash<QString, UpdateManifest> available2;
  available2.insert(v1.version(), v1);
  available2.insert(v2d.version(), v2d);
  QCOMPARE(UpdateManifest::resolveChain(v2d, QStringLiteral("4.3.0"), available2).status,
           UpdateManifest::ChainStatus::MissingDelta);
}

void TestUpdateManifest::testChainHopCap() {
  // Build 4.0.0 .. 4.0.9, each pointing at its predecessor. Walking from the
  // newest back to 4.0.0 needs 9 hops, above the cap of 5.
  QHash<QString, UpdateManifest> available;
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};
  for (int i = 0; i <= 9; ++i) {
    const QString ver = QStringLiteral("4.0.%1").arg(i);
    const QString base = i == 0 ? QString() : QStringLiteral("4.0.%1").arg(i - 1);
    available.insert(ver, make(ver, files, base, 1, QStringLiteral("stable"), 100 * 1000 * 1000));
  }

  const auto newest = available.value(QStringLiteral("4.0.9"));
  QCOMPARE(UpdateManifest::resolveChain(newest, QStringLiteral("4.0.0"), available).status,
           UpdateManifest::ChainStatus::TooManyHops);

  // Exactly at the cap succeeds.
  const auto atCap = UpdateManifest::resolveChain(newest, QStringLiteral("4.0.4"), available);
  QCOMPARE(atCap.status, UpdateManifest::ChainStatus::Ok);
  QCOMPARE(atCap.hopVersions.size(), UpdateManifest::c_maxChainHops);
}

void TestUpdateManifest::testChainSizeCap() {
  // Target expands to 1000 bytes; the cap is 60% == 600.
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};
  const auto v0 = make(QStringLiteral("4.3.0"), files, QString(), 0, QStringLiteral("stable"), 1000);
  const auto tooBig =
      make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.0"), 601, QStringLiteral("stable"),
           1000);
  const auto justFits =
      make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.0"), 600, QStringLiteral("stable"),
           1000);

  QHash<QString, UpdateManifest> available;
  available.insert(v0.version(), v0);

  available.insert(tooBig.version(), tooBig);
  const auto over = UpdateManifest::resolveChain(tooBig, QStringLiteral("4.3.0"), available);
  QCOMPARE(over.status, UpdateManifest::ChainStatus::TooLarge);
  QVERIFY(over.hopVersions.isEmpty());

  available.insert(justFits.version(), justFits);
  const auto under = UpdateManifest::resolveChain(justFits, QStringLiteral("4.3.0"), available);
  QCOMPARE(under.status, UpdateManifest::ChainStatus::Ok);
}

void TestUpdateManifest::testChainRejectsNonStableIntermediate() {
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};
  const auto v0 = make(QStringLiteral("4.3.0"), files, QString(), 0, QStringLiteral("stable"),
                       10 * 1000 * 1000);
  // 4.3.1 is a continuous build that somehow got a manifest published.
  const auto v1 = make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.0"), 1000,
                       QStringLiteral("continuous"), 10 * 1000 * 1000);
  const auto v2 = make(QStringLiteral("4.3.2"), files, QStringLiteral("4.3.1"), 1000,
                       QStringLiteral("stable"), 10 * 1000 * 1000);

  QHash<QString, UpdateManifest> available;
  available.insert(v0.version(), v0);
  available.insert(v1.version(), v1);
  available.insert(v2.version(), v2);

  QCOMPARE(UpdateManifest::resolveChain(v2, QStringLiteral("4.3.0"), available).status,
           UpdateManifest::ChainStatus::NonStableBase);
}

void TestUpdateManifest::testChainRejectsVariantMismatch() {
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};

  QJsonObject o = baseObject(QStringLiteral("4.3.0"));
  o[QStringLiteral("variant")] = QStringLiteral("win64-windows7");
  const auto v0 = UpdateManifest::fromJson(o);
  QVERIFY(v0.isValid());

  const auto v1 = make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.0"), 1000,
                       QStringLiteral("stable"), 10 * 1000 * 1000);

  QHash<QString, UpdateManifest> available;
  available.insert(v0.version(), v0);
  available.insert(v1.version(), v1);

  // v1 is "win64" but its declared base is a "win64-windows7" manifest.
  QCOMPARE(UpdateManifest::resolveChain(v1, QStringLiteral("4.2.9"), available).status,
           UpdateManifest::ChainStatus::BrokenChain);
}

void TestUpdateManifest::testChainRejectsCycle() {
  const QVector<QPair<QString, QString>> files{{QStringLiteral("a.dll"), fakeHash('a')}};
  const auto a = make(QStringLiteral("4.3.1"), files, QStringLiteral("4.3.2"), 1,
                      QStringLiteral("stable"), 10 * 1000 * 1000);
  const auto b = make(QStringLiteral("4.3.2"), files, QStringLiteral("4.3.1"), 1,
                      QStringLiteral("stable"), 10 * 1000 * 1000);

  QHash<QString, UpdateManifest> available;
  available.insert(a.version(), a);
  available.insert(b.version(), b);

  // Never reaches 4.0.0 and must not spin.
  const auto r = UpdateManifest::resolveChain(b, QStringLiteral("4.0.0"), available);
  QCOMPARE(r.status, UpdateManifest::ChainStatus::BrokenChain);
}

// --------------------------------------------------------- base identity

void TestUpdateManifest::testValidateBaseIdentityMatches() {
  const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')},
                                                    {QStringLiteral("b/c.dll"), fakeHash('b')}});
  const auto published = make(QStringLiteral("4.3.1"), {{QStringLiteral("b/c.dll"), fakeHash('b')},
                                                        {QStringLiteral("a.dll"), fakeHash('a')}});

  QString err;
  QVERIFY2(UpdateManifest::validateBaseIdentity(local, published, &err), qPrintable(err));

  // An invalid manifest on either side fails cleanly rather than crashing.
  QVERIFY(!UpdateManifest::validateBaseIdentity(UpdateManifest(), published));
  QVERIFY(!UpdateManifest::validateBaseIdentity(local, UpdateManifest()));
}

void TestUpdateManifest::testValidateBaseIdentityVersionMismatch() {
  const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
  const auto published = make(QStringLiteral("4.3.2"), {{QStringLiteral("a.dll"), fakeHash('a')}});
  QString err;
  QVERIFY(!UpdateManifest::validateBaseIdentity(local, published, &err));
  QVERIFY2(err.contains(QStringLiteral("version")), qPrintable(err));
}

void TestUpdateManifest::testValidateBaseIdentityCommitMismatch() {
  const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});

  QJsonObject o = baseObject(QStringLiteral("4.3.1"));
  o[QStringLiteral("commit")] = QStringLiteral("cafebabe");
  o[QStringLiteral("files")] = QJsonArray{fileEntry(QStringLiteral("a.dll"), 100, fakeHash('a'))};
  const auto published = UpdateManifest::fromJson(o);
  QVERIFY(published.isValid());

  QString err;
  QVERIFY(!UpdateManifest::validateBaseIdentity(local, published, &err));
  QVERIFY2(err.contains(QStringLiteral("commit")), qPrintable(err));
}

void TestUpdateManifest::testValidateBaseIdentityFilesMismatch() {
  // Extra file locally (drift).
  {
    const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')},
                                                      {QStringLiteral("extra.dll"), fakeHash('x')}});
    const auto published = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
    QString err;
    QVERIFY(!UpdateManifest::validateBaseIdentity(local, published, &err));
    QVERIFY2(err.contains(QStringLiteral("size mismatch")), qPrintable(err));
  }
  // Same path count, different hash.
  {
    const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
    const auto published = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('z')}});
    QString err;
    QVERIFY(!UpdateManifest::validateBaseIdentity(local, published, &err));
    QVERIFY2(err.contains(QStringLiteral("a.dll")), qPrintable(err));
  }
  // Same path count, different path.
  {
    const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}});
    const auto published = make(QStringLiteral("4.3.1"), {{QStringLiteral("b.dll"), fakeHash('a')}});
    QString err;
    QVERIFY(!UpdateManifest::validateBaseIdentity(local, published, &err));
    QVERIFY2(err.contains(QStringLiteral("missing")), qPrintable(err));
  }
  // Same path + hash, different SIZE. A size-only divergence still means the
  // local tree is not the published base.
  {
    const auto local = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}},
                            QString(), 1000, QStringLiteral("stable"), 100);
    const auto published = make(QStringLiteral("4.3.1"), {{QStringLiteral("a.dll"), fakeHash('a')}},
                                QString(), 1000, QStringLiteral("stable"), 200);
    QVERIFY(!UpdateManifest::validateBaseIdentity(local, published));
  }
}

// -------------------------------------------------------------- path safety

void TestUpdateManifest::testNormalizePathAccepts() {
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("vnote.exe")),
           QStringLiteral("vnote.exe"));
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("platforms/qwindows.dll")),
           QStringLiteral("platforms/qwindows.dll"));
  // Backslashes are normalized to forward slashes.
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("translations\\vnote_zh_CN.qm")),
           QStringLiteral("translations/vnote_zh_CN.qm"));
  // Deeply nested and Unicode names are fine.
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("a/b/c/d/e.dll")),
           QStringLiteral("a/b/c/d/e.dll"));
  QCOMPARE(UpdateManifest::normalizePath(QString::fromUtf8("资源/图标.png")),
           QString::fromUtf8("资源/图标.png"));
  // A name that merely CONTAINS a reserved stem is fine.
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("console.dll")),
           QStringLiteral("console.dll"));
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral("a/nullptr.h")),
           QStringLiteral("a/nullptr.h"));
  // A leading dot is fine (dot-directories are real).
  QCOMPARE(UpdateManifest::normalizePath(QStringLiteral(".keep")), QStringLiteral(".keep"));
}

void TestUpdateManifest::testNormalizePathRejects_data() {
  QTest::addColumn<QString>("path");

  QTest::newRow("empty") << QString();
  QTest::newRow("dotdot") << QStringLiteral("../evil.dll");
  QTest::newRow("dotdot-nested") << QStringLiteral("a/../../evil.dll");
  QTest::newRow("dotdot-trailing") << QStringLiteral("a/..");
  QTest::newRow("dot-segment") << QStringLiteral("a/./b.dll");
  QTest::newRow("absolute-slash") << QStringLiteral("/etc/passwd");
  QTest::newRow("absolute-backslash") << QStringLiteral("\\windows\\system32\\x.dll");
  QTest::newRow("drive-qualified") << QStringLiteral("C:/windows/x.dll");
  QTest::newRow("drive-relative") << QStringLiteral("C:x.dll");
  QTest::newRow("unc") << QStringLiteral("//server/share/x.dll");
  QTest::newRow("device-qmark") << QStringLiteral("\\\\?\\C:\\x.dll");
  QTest::newRow("device-dot") << QStringLiteral("\\\\.\\PhysicalDrive0");
  QTest::newRow("double-slash") << QStringLiteral("a//b.dll");
  QTest::newRow("trailing-slash") << QStringLiteral("a/b/");
  QTest::newRow("reserved-con") << QStringLiteral("CON");
  QTest::newRow("reserved-nul-ext") << QStringLiteral("NUL.txt");
  QTest::newRow("reserved-com1") << QStringLiteral("a/COM1");
  QTest::newRow("reserved-lpt9-ext") << QStringLiteral("a/lpt9.dll");
  QTest::newRow("reserved-aux-lower") << QStringLiteral("aux");
  QTest::newRow("trailing-dot") << QStringLiteral("a/b.");
  QTest::newRow("trailing-space") << QStringLiteral("a/b ");
  QTest::newRow("leading-space") << QStringLiteral("a/ b");
  QTest::newRow("colon") << QStringLiteral("a/b:stream");
  QTest::newRow("wildcard-star") << QStringLiteral("a/*.dll");
  QTest::newRow("wildcard-qmark") << QStringLiteral("a/?.dll");
  QTest::newRow("pipe") << QStringLiteral("a/b|c");
  QTest::newRow("quote") << QStringLiteral("a/b\"c");
  QTest::newRow("lt") << QStringLiteral("a/b<c");
  QTest::newRow("gt") << QStringLiteral("a/b>c");
  QTest::newRow("control-char") << QStringLiteral("a/b\x01/c.dll");
  QTest::newRow("newline") << QStringLiteral("a/b\nc");
}

void TestUpdateManifest::testNormalizePathRejects() {
  QFETCH(QString, path);
  QVERIFY2(UpdateManifest::normalizePath(path).isEmpty(), qPrintable(path));
}

void TestUpdateManifest::testPathKeyIsCaseFolded() {
  QCOMPARE(UpdateManifest::pathKey(QStringLiteral("Resources/Icon.PNG")),
           UpdateManifest::pathKey(QStringLiteral("resources/icon.png")));

  QVERIFY(UpdateManifest::isReservedPath(QStringLiteral(".vnote-update/staged/a.dll")));
  QVERIFY(UpdateManifest::isReservedPath(QStringLiteral(".VNote-Update/staged/a.dll")));
  QVERIFY(UpdateManifest::isReservedPath(QStringLiteral(".vnote-old/1/a.dll")));
  QVERIFY(!UpdateManifest::isReservedPath(QStringLiteral(".vnote-updater/a.dll")));
  QVERIFY(!UpdateManifest::isReservedPath(QStringLiteral("vnote.exe")));
}

void TestUpdateManifest::testVariantForBuild() {
  const QString v = UpdateManifest::variantForBuild();
  QVERIFY2(v == QStringLiteral("win64") || v == QStringLiteral("win64-windows7"), qPrintable(v));
#if QT_VERSION_MAJOR >= 6
  QCOMPARE(v, QStringLiteral("win64"));
#endif
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUpdateManifest)
#include "test_updatemanifest.moc"
