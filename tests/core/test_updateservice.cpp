// End-to-end unit tests for UpdateService, the CHECK -> PLAN -> DOWNLOAD ->
// STAGE half of the incremental updater.
//
// UpdateService is the only major unit of the feature that had no automated
// coverage, and it holds the most intricate logic: signature-verified fetch,
// delta chain resolution, drift detection, hop overlay, pruning and
// pending.json production. Everything it decides steers bytes that VNote will
// later EXECUTE, so the cases below lean hard on the rejection paths.
//
// Design notes for anyone extending this file:
//
//   * The REAL QNetworkAccessManager path is exercised against a local HTTP
//     server rather than mocked, so redirects, the host allowlist, byte caps and
//     cancellation are covered as shipped. The service is pointed at it through
//     the production seams testSetEndpointOverride() / testSetExtraAllowedHost();
//     the latter is what permits plain HTTP, and ONLY for the named host.
//
//   * Signatures are produced IN-TEST with the vendored primitives. Ed25519
//     signing needs no randomness (only key GENERATION does), so a fixed secret
//     key is embedded and libs/minicrypto/randombytes_stub.c -- which aborts by
//     design -- is never reached. If a run ever aborts inside randombytes, some
//     new code is calling key generation, which is not allowed here.
//     testSigningFixtureIsAcceptedByTheVerifier is the canary that tells a
//     broken FIXTURE apart from a broken SERVICE.
//
//   * The trusted-comment prefix is "trusted comment: " INCLUDING the trailing
//     space (see manifestsignature.cpp). Getting that wrong makes every global
//     signature check fail for reasons that look nothing like the real cause.
//
// Not covered here: nothing. The one branch that used to be unreachable from
// the outside -- `hasTrustedKeys() == false`, i.e. an unconfigured build --
// now has its own seam, `ManifestSignature::testClearTrustedKeys()`. It is
// deliberately SEPARATE from testSetTrustedKeys({}), which means "restore the
// production keys" and is therefore what cleanup() calls.
//
// Scope: this suite is skipped off Windows. The updater is Windows x64 only by
// design, so checkEligibility() reports ineligible everywhere else and most
// cases below could never reach planning or staging. The target still COMPILES
// on every platform, which is the part worth keeping.

#include <QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <core/manifestsignature.h>
#include <core/services/updateservice.h>
#include <core/updateinstaller.h>
#include <core/updatemanifest.h>
#include <core/zipextractor.h>

// blake2.h carries its own extern "C" guards; tweetnacl.h does NOT.
extern "C" {
#include <tweetnacl.h>
}

#include <blake2.h>

using namespace vnotex;

namespace tests {

namespace {

using FileSet = QVector<QPair<QString, QByteArray>>;

// --------------------------------------------------------------------------
// Signing fixtures
// --------------------------------------------------------------------------

// A throwaway Ed25519 keypair, generated once with PyNaCl and embedded as a
// seed + public key. The seed is all crypto_sign() needs; nothing here ever
// calls crypto_sign_keypair(), which is the only function that would touch the
// aborting randombytes stub.
struct TestKey {
  QByteArray keyId;     // 8 bytes, arbitrary (minisign key ids are not derived)
  QByteArray secretKey; // 64 bytes: seed || public key, the TweetNaCl layout
  QByteArray publicKey; // 32 bytes
};

TestKey makeKey(const char *p_seedHex, const char *p_publicHex, const char *p_keyIdHex) {
  TestKey key;
  key.publicKey = QByteArray::fromHex(p_publicHex);
  key.keyId = QByteArray::fromHex(p_keyIdHex);
  key.secretKey = QByteArray::fromHex(p_seedHex) + key.publicKey;
  return key;
}

// The key the tests configure as trusted.
const TestKey &trustedKey() {
  static const TestKey key = makeKey(
      "5ac7f6ef198576ea422679bb4d1f4951abd2e0d388dbe1e785b229cf3bcd6c59",
      "334549b605c1a1c2c0e92d0165cce2846971d8f900235b2465f454a7da58e7b9", "1122334455667788");
  return key;
}

// An independent key that is never trusted: stands in for a compromised or
// simply foreign signer.
const TestKey &foreignKey() {
  static const TestKey key = makeKey(
      "0d45da736e7582141a3554c7831e80159c1800db051e58a0cd8887d7f860ce28",
      "183a79a7aff319886c446c6fdf32d94ca0f51629be257e953e814f33f56c8446", "99aabbccddeeff00");
  return key;
}

ManifestSignature::PublicKey publicKeyOf(const TestKey &p_key) {
  ManifestSignature::PublicKey out;
  out.keyId = p_key.keyId;
  out.publicKey = p_key.publicKey;
  return out;
}

QByteArray blake2b512(const QByteArray &p_data) {
  QByteArray digest(64, Qt::Uninitialized);
  const int rc = blake2b(digest.data(), 64, p_data.constData(), static_cast<size_t>(p_data.size()),
                         nullptr, 0);
  return rc == 0 ? digest : QByteArray();
}

// Detached Ed25519 signature. TweetNaCl only offers the combined form, so the
// leading 64 bytes of the signed message are taken.
QByteArray ed25519Sign(const QByteArray &p_message, const QByteArray &p_secretKey) {
  QByteArray signedMessage(p_message.size() + crypto_sign_BYTES, Qt::Uninitialized);
  unsigned long long signedLen = 0;
  crypto_sign(reinterpret_cast<unsigned char *>(signedMessage.data()), &signedLen,
              reinterpret_cast<const unsigned char *>(p_message.constData()),
              static_cast<unsigned long long>(p_message.size()),
              reinterpret_cast<const unsigned char *>(p_secretKey.constData()));
  return signedMessage.left(crypto_sign_BYTES);
}

// Assembles a minisign .minisig for the PREHASHED (`ED`) algorithm: the file
// signature covers BLAKE2b-512(message), and the global signature covers
// signature || trustedComment.
QByteArray makeSignature(const TestKey &p_key, const QByteArray &p_message,
                         const QByteArray &p_trustedComment) {
  const QByteArray signature = ed25519Sign(blake2b512(p_message), p_key.secretKey);

  QByteArray blob;
  blob.append("ED", 2);
  blob.append(p_key.keyId);
  blob.append(signature);

  QByteArray globalPayload = signature;
  globalPayload.append(p_trustedComment);
  const QByteArray globalSignature = ed25519Sign(globalPayload, p_key.secretKey);

  QByteArray out;
  out.append("untrusted comment: signature from a vnote test key\n");
  out.append(blob.toBase64());
  out.append('\n');
  // The trailing space in the prefix is part of the format.
  out.append("trusted comment: ");
  out.append(p_trustedComment);
  out.append('\n');
  out.append(globalSignature.toBase64());
  out.append('\n');
  return out;
}

// --------------------------------------------------------------------------
// Small filesystem / JSON helpers
// --------------------------------------------------------------------------

QByteArray sha256Hex(const QByteArray &p_data) {
  return QCryptographicHash::hash(p_data, QCryptographicHash::Sha256).toHex();
}

// Highly compressible payloads keep the delta archives far below
// UpdateManifest::c_maxChainSizeRatio of the target's expanded size, so the
// chain is accepted for the reason under test rather than rejected as TooLarge.
QByteArray blobOf(const char *p_marker) { return QByteArray(p_marker).repeated(2048); }

bool writeFileAt(const QString &p_path, const QByteArray &p_data) {
  QDir().mkpath(QFileInfo(p_path).absolutePath());
  QFile file(p_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const bool ok = file.write(p_data) == p_data.size();
  file.close();
  return ok;
}

QByteArray readFileAt(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}

QStringList relativeFilesUnder(const QString &p_root) {
  QStringList out;
  QDirIterator it(p_root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  const QDir root(p_root);
  while (it.hasNext()) {
    out.append(root.relativeFilePath(it.next()));
  }
  out.sort();
  return out;
}

} // namespace

// --------------------------------------------------------------------------
// Local HTTP server
// --------------------------------------------------------------------------
//
// Deliberately real HTTP over a real socket: the point is to drive the shipped
// QNetworkAccessManager code path, including manual redirect following, the
// allowlist check on EVERY hop, and the 250 ms cancellation poll.
class TestHttpServer : public QTcpServer {
  Q_OBJECT

public:
  struct Response {
    int status = 200;
    QByteArray body;
    QByteArray location; // only for 3xx
    int delayMs = 0;
  };

  explicit TestHttpServer(QObject *p_parent = nullptr) : QTcpServer(p_parent) {}

  bool start() { return listen(QHostAddress::LocalHost, 0); }

  QUrl urlFor(const QString &p_path) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(p_path));
  }

  void serve(const QString &p_path, const QByteArray &p_body) {
    Response response;
    response.body = p_body;
    m_routes.insert(p_path, response);
  }

  void serveStatus(const QString &p_path, int p_status) {
    Response response;
    response.status = p_status;
    m_routes.insert(p_path, response);
  }

  void serveRedirect(const QString &p_path, const QString &p_location, int p_status = 302) {
    Response response;
    response.status = p_status;
    response.location = p_location.toUtf8();
    m_routes.insert(p_path, response);
  }

  void setDelay(const QString &p_path, int p_delayMs) {
    if (m_routes.contains(p_path)) {
      m_routes[p_path].delayMs = p_delayMs;
    }
  }

  void removeRoute(const QString &p_path) { m_routes.remove(p_path); }

  QStringList requestedPaths() const { return m_requests; }
  void clearRequests() { m_requests.clear(); }

signals:
  // Lets a test act the moment a specific request is genuinely in flight,
  // instead of guessing at a delay.
  void requestReceived(const QString &p_path);

protected:
  void incomingConnection(qintptr p_socketDescriptor) override {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(p_socketDescriptor)) {
      delete socket;
      return;
    }
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { onReadyRead(socket); });
    // Deliberately touches NOTHING on the server. A socket that is still alive
    // when the server is torn down (the cancellation case leaves one parked on
    // a delayed reply) is destroyed as a child AFTER ~TestHttpServer has run,
    // and QTcpSocket's destructor emits disconnected() on the way out -- so a
    // handler that reached back into a server member would be a use-after-free.
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  }

private:
  void onReadyRead(QTcpSocket *p_socket) {
    // The pending request lives on the socket itself, for the reason above.
    static const char *const c_bufferProperty = "vnoteRequestBuffer";
    QByteArray buffer = p_socket->property(c_bufferProperty).toByteArray();
    buffer.append(p_socket->readAll());
    if (!buffer.contains("\r\n\r\n")) {
      p_socket->setProperty(c_bufferProperty, buffer);
      return;
    }

    const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    const QString path = parts.size() >= 2 ? QString::fromUtf8(parts.at(1)) : QString();
    m_requests.append(path);
    emit requestReceived(path);
    // Only one request per connection: every response says Connection: close.
    p_socket->setProperty(c_bufferProperty, QByteArray());

    const Response response = m_routes.contains(path) ? m_routes.value(path) : notFoundResponse();

    if (response.delayMs > 0) {
      // Bound to the socket, so a client that aborts (the cancellation case)
      // simply never gets a reply instead of writing through a dead pointer.
      QTimer::singleShot(response.delayMs, p_socket,
                         [p_socket, response]() { writeResponse(p_socket, response); });
      return;
    }
    writeResponse(p_socket, response);
  }

  static Response notFoundResponse() {
    Response response;
    response.status = 404;
    return response;
  }

  static QByteArray statusText(int p_status) {
    switch (p_status) {
    case 200:
      return QByteArrayLiteral("200 OK");
    case 301:
      return QByteArrayLiteral("301 Moved Permanently");
    case 302:
      return QByteArrayLiteral("302 Found");
    case 404:
      return QByteArrayLiteral("404 Not Found");
    default:
      return QByteArrayLiteral("500 Internal Server Error");
    }
  }

  static void writeResponse(QTcpSocket *p_socket, const Response &p_response) {
    QByteArray head = "HTTP/1.1 " + statusText(p_response.status) + "\r\n";
    if (!p_response.location.isEmpty()) {
      head += "Location: " + p_response.location + "\r\n";
    }
    head += "Content-Type: application/octet-stream\r\n";
    head += "Content-Length: " + QByteArray::number(p_response.body.size()) + "\r\n";
    head += "Connection: close\r\n\r\n";

    p_socket->write(head);
    if (!p_response.body.isEmpty()) {
      p_socket->write(p_response.body);
    }
    p_socket->flush();
    p_socket->disconnectFromHost();
  }

  QHash<QString, Response> m_routes;
  QStringList m_requests;
};

// --------------------------------------------------------------------------
// The test
// --------------------------------------------------------------------------

class TestUpdateService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  // Fixture canary.
  void testSigningFixtureIsAcceptedByTheVerifier();

  // Signature enforcement -- the code-execution path.
  void testSignedFullPackagePlanProceeds();
  void testTamperedManifestIsRefused();
  void testUntrustedSigningKeyIsRefused();
  void testMissingSignatureIsRefused();
  void testForgedIntermediateManifestFallsBackToFullPackage();
  void testForgedPublishedBaseManifestFallsBackToFullPackage();
  void testNoTrustedKeysMakesTheInstallIneligible();

  // Version / replay.
  void testManifestDeclaringAnotherVersionIsRefused();
  void testOlderOrEqualReleaseOffersNoUpdate();

  // Planning.
  void testDeltaSingleHopIsPlanned();
  void testNoLocalManifestFallsBackToFullPackage();
  void testContinuousLocalChannelFallsBackToFullPackage();
  void testLocalDriftFallsBackToFullPackage();
  void testOversizedChainFallsBackToFullPackage();

  // Staging / output.
  void testDeltaStagingAppliesHopsOldestFirstAndPrunes();
  void testHopArchiveWithAnExtraEntryIsRejected();
  void testFullPackageWithAnUnexpectedEntryIsRejected();
  void testFullPackageWithoutAnInPackageManifestIsRejected();
  void testFullPackageIdentityMismatchIsRejected();
  void testFullPackageManifestDisagreementIsRejected();
  void testFullPackageStagingProducesAUsablePendingPlan();

  // Pending lifecycle.
  void testRevalidatePendingAcceptsAConsistentPlan();
  void testRevalidatePendingDiscardsAForeignVariant();
  void testRevalidatePendingDiscardsAnAlreadyAppliedTarget();
  void testRevalidatePendingDiscardsATamperedStagedFile();
  void testConsumeStoredResultReadsAndClears();

  // Networking.
  void testRedirectWithinTheAllowlistIsFollowed();
  void testRedirectToAnUnexpectedHostIsRefused();
  void testRedirectDowngradingToPlainHttpIsRefused();
  void testTooManyRedirectsAreRefused();
  void testDownloadLargerThanDeclaredIsAborted();
  void testCancelDuringDownloadReturnsPromptly();

  // Release source.
  void testSourceStringRoundTrip();
  void testAllowlistIsPerSource();
  void testGiteeRedirectToGitHubHostsIsRefused();
  void testGitHubRedirectToGiteeHostsIsRefused();
  void testReleasesPageUrlFollowsTheSource();
  void testGiteeSynthesizesTheReleasePageUrl();
  void testMissingManifestDegradesToCheckOnly();

  // Eligibility.
  void testPackagedAppIsIneligibleWithAStoreReason();

  // startDownload() acceptance contract.
  void testStartDownloadWithoutAPlanReportsNoPlan();
  void testStartDownloadRefusesAStaleExpectedVersion();
  void testStartDownloadIsBusyWhileACheckIsRunning();

private:
  // ---- fixture plumbing -------------------------------------------------
  struct PublishOptions {
    QString channel = QStringLiteral("stable");
    const TestKey *key = nullptr; // defaults to trustedKey()
    bool serveSignature = true;
    bool tamperManifestAfterSigning = false;
    // When set, the published manifest declares THIS version while still being
    // served at the URL of the version it was published under.
    QString declaredVersion;
    bool withFullPackage = true;
    bool omitInPackageManifest = false;
    bool disagreeingInPackageManifest = false;
    bool identityMismatchInPackageManifest = false;
    FileSet extraFullEntries;
    FileSet extraDeltaEntries;
    qint64 declaredDeltaSize = -1;
  };

  struct Release {
    QJsonObject core;
    UpdateManifest filesManifest;
    FileSet files;
    QByteArray fullZip;
    QByteArray deltaZip;
  };

  QString variant() const { return UpdateManifest::variantForBuild(); }
  QString manifestAsset(const QString &p_version) const;
  QString fullPackageAsset(const QString &p_version) const;
  QString deltaAsset(const QString &p_version) const;
  QString assetPath(const QString &p_version, const QString &p_asset) const;

  QJsonObject manifestCore(const QString &p_version, const FileSet &p_files,
                           const QString &p_channel) const;

  void publish(const QString &p_version, const FileSet &p_files,
               const QString &p_deltaBase = QString(),
               const PublishOptions &p_options = PublishOptions());

  void setLatestRelease(const QString &p_version);

  void installRelease(const QString &p_version, bool p_withLocalManifest = true);

  UpdateService *makeService(const QString &p_currentVersion);

  // ---- async drivers ----------------------------------------------------
  struct Outcome {
    bool finished = false;
    bool ok = false;
    UpdateInfo info;
    QString version;
    QString error;
  };

  Outcome runCheck(UpdateService *p_service, int p_timeoutMs = 30000);
  Outcome runDownload(UpdateService *p_service, int p_timeoutMs = 30000);

  // ---- standard file sets ------------------------------------------------
  static FileSet filesV0();
  static FileSet filesV1();
  static FileSet filesV2();

  QString stagedDir() const { return UpdateInstaller::stagedDir(m_installDir); }

  QScopedPointer<QTemporaryDir> m_temp;
  QString m_installDir;
  QString m_scratchDir;
  TestHttpServer *m_server = nullptr;
  QHash<QString, Release> m_releases;
  QScopedPointer<UpdateService> m_service;
};

// ---------------------------------------------------------------- fixtures

FileSet TestUpdateService::filesV0() {
  FileSet files;
  files.append(qMakePair(QStringLiteral("vnote.exe"), blobOf("EXE-v0-")));
  files.append(qMakePair(QStringLiteral("Qt6Core.dll"), blobOf("QTCORE-v0-")));
  files.append(qMakePair(QStringLiteral("Qt6Gui.dll"), blobOf("QTGUI-stable-")));
  files.append(qMakePair(QStringLiteral("resources/logo.png"), blobOf("LOGO-stable-")));
  files.append(qMakePair(QStringLiteral("obsolete.dll"), blobOf("OBSOLETE-")));
  return files;
}

// 4.3.1: vnote.exe and Qt6Core.dll change, obsolete.dll is gone, and
// plugins/extra.dll appears. Qt6Gui.dll and the logo are untouched.
FileSet TestUpdateService::filesV1() {
  FileSet files;
  files.append(qMakePair(QStringLiteral("vnote.exe"), blobOf("EXE-v1-")));
  files.append(qMakePair(QStringLiteral("Qt6Core.dll"), blobOf("QTCORE-v1-")));
  files.append(qMakePair(QStringLiteral("Qt6Gui.dll"), blobOf("QTGUI-stable-")));
  files.append(qMakePair(QStringLiteral("resources/logo.png"), blobOf("LOGO-stable-")));
  files.append(qMakePair(QStringLiteral("plugins/extra.dll"), blobOf("EXTRA-v1-")));
  return files;
}

// 4.3.2: vnote.exe changes AGAIN (so hop ordering is observable), Qt6Core.dll is
// REVERTED to its 4.3.0 bytes, and plugins/extra.dll -- which only ever existed
// in 4.3.1 -- disappears.
FileSet TestUpdateService::filesV2() {
  FileSet files;
  files.append(qMakePair(QStringLiteral("vnote.exe"), blobOf("EXE-v2-")));
  files.append(qMakePair(QStringLiteral("Qt6Core.dll"), blobOf("QTCORE-v0-")));
  files.append(qMakePair(QStringLiteral("Qt6Gui.dll"), blobOf("QTGUI-stable-")));
  files.append(qMakePair(QStringLiteral("resources/logo.png"), blobOf("LOGO-stable-")));
  return files;
}

QString TestUpdateService::manifestAsset(const QString &p_version) const {
  return QStringLiteral("VNote-%1-%2.manifest.json").arg(p_version, variant());
}

QString TestUpdateService::fullPackageAsset(const QString &p_version) const {
  return QStringLiteral("VNote-%1-%2.zip").arg(p_version, variant());
}

QString TestUpdateService::deltaAsset(const QString &p_version) const {
  return QStringLiteral("VNote-%1-%2.delta.zip").arg(p_version, variant());
}

QString TestUpdateService::assetPath(const QString &p_version, const QString &p_asset) const {
  return QStringLiteral("/download/v%1/%2").arg(p_version, p_asset);
}

QJsonObject TestUpdateService::manifestCore(const QString &p_version, const FileSet &p_files,
                                            const QString &p_channel) const {
  QJsonObject core;
  core[QStringLiteral("schema")] = 1;
  core[QStringLiteral("product")] = QStringLiteral("VNote");
  core[QStringLiteral("channel")] = p_channel;
  core[QStringLiteral("version")] = p_version;
  core[QStringLiteral("variant")] = variant();
  core[QStringLiteral("platform")] = QStringLiteral("windows-x64");
  core[QStringLiteral("commit")] = QStringLiteral("commit-for-%1").arg(p_version);
  core[QStringLiteral("generatedAt")] = QStringLiteral("2026-08-01T12:00:00Z");

  QJsonArray filesArray;
  for (const auto &entry : p_files) {
    QJsonObject fileObject;
    fileObject[QStringLiteral("path")] = entry.first;
    fileObject[QStringLiteral("size")] = static_cast<double>(entry.second.size());
    fileObject[QStringLiteral("sha256")] = QString::fromLatin1(sha256Hex(entry.second));
    filesArray.append(fileObject);
  }
  core[QStringLiteral("files")] = filesArray;
  return core;
}

void TestUpdateService::publish(const QString &p_version, const FileSet &p_files,
                                const QString &p_deltaBase, const PublishOptions &p_options) {
  Release release;
  release.files = p_files;
  release.core = manifestCore(p_version, p_files, p_options.channel);

  QString parseError;
  release.filesManifest = UpdateManifest::fromJson(release.core, &parseError);
  QVERIFY2(release.filesManifest.isValid(), qPrintable(parseError));

  // --- full package -------------------------------------------------------
  QJsonObject published = release.core;
  if (!p_options.declaredVersion.isEmpty()) {
    published[QStringLiteral("version")] = p_options.declaredVersion;
  }

  if (p_options.withFullPackage) {
    const QString topDir = QStringLiteral("VNote-%1-%2").arg(p_version, variant());
    FileSet entries;
    for (const auto &entry : p_files) {
      entries.append(qMakePair(topDir + QLatin1Char('/') + entry.first, entry.second));
    }
    for (const auto &entry : p_options.extraFullEntries) {
      entries.append(qMakePair(topDir + QLatin1Char('/') + entry.first, entry.second));
    }
    if (!p_options.omitInPackageManifest) {
      QJsonObject inPackage = release.core;
      if (p_options.identityMismatchInPackageManifest) {
        // Identity only: the file map still agrees, so ONLY the identity
        // comparison can reject this package.
        inPackage[QStringLiteral("commit")] = QStringLiteral("a-completely-different-commit");
      }
      if (p_options.disagreeingInPackageManifest) {
        // Same identity, different files[]: the exact poisoning that would
        // silently break every later delta if it were accepted.
        QJsonArray filesArray = inPackage.value(QStringLiteral("files")).toArray();
        QJsonObject first = filesArray.at(0).toObject();
        first[QStringLiteral("sha256")] =
            QString::fromLatin1(sha256Hex(QByteArrayLiteral("a different file entirely")));
        filesArray.replace(0, first);
        inPackage[QStringLiteral("files")] = filesArray;
      }
      entries.append(qMakePair(topDir + QStringLiteral("/manifest.json"),
                               QJsonDocument(inPackage).toJson(QJsonDocument::Indented)));
    }

    const QString archivePath = m_scratchDir + QLatin1Char('/') + fullPackageAsset(p_version);
    QVERIFY(ZipExtractor::createArchive(archivePath, entries));
    release.fullZip = readFileAt(archivePath);
    QVERIFY(!release.fullZip.isEmpty());

    QJsonObject ref;
    ref[QStringLiteral("asset")] = fullPackageAsset(p_version);
    ref[QStringLiteral("size")] = static_cast<double>(release.fullZip.size());
    ref[QStringLiteral("sha256")] = QString::fromLatin1(sha256Hex(release.fullZip));
    published[QStringLiteral("fullPackage")] = ref;

    m_server->serve(assetPath(p_version, fullPackageAsset(p_version)), release.fullZip);
  }

  // --- delta --------------------------------------------------------------
  if (!p_deltaBase.isEmpty()) {
    QVERIFY2(m_releases.contains(p_deltaBase), "publish the delta base first");
    const UpdateManifest baseManifest = m_releases.value(p_deltaBase).filesManifest;

    QHash<QString, QByteArray> contentByKey;
    for (const auto &entry : p_files) {
      contentByKey.insert(UpdateManifest::pathKey(entry.first), entry.second);
    }

    FileSet entries;
    const QStringList hopSet = UpdateManifest::hopArchiveSet(baseManifest, release.filesManifest);
    for (const QString &path : hopSet) {
      entries.append(qMakePair(path, contentByKey.value(UpdateManifest::pathKey(path))));
    }
    entries += p_options.extraDeltaEntries;

    const QString archivePath = m_scratchDir + QLatin1Char('/') + deltaAsset(p_version);
    QVERIFY(ZipExtractor::createArchive(archivePath, entries));
    release.deltaZip = readFileAt(archivePath);
    QVERIFY(!release.deltaZip.isEmpty());

    QJsonObject ref;
    ref[QStringLiteral("asset")] = deltaAsset(p_version);
    ref[QStringLiteral("size")] = static_cast<double>(
        p_options.declaredDeltaSize >= 0 ? p_options.declaredDeltaSize : release.deltaZip.size());
    ref[QStringLiteral("sha256")] = QString::fromLatin1(sha256Hex(release.deltaZip));
    ref[QStringLiteral("baseVersion")] = p_deltaBase;
    published[QStringLiteral("delta")] = ref;

    m_server->serve(assetPath(p_version, deltaAsset(p_version)), release.deltaZip);
  }

  // --- manifest + signature ----------------------------------------------
  const QByteArray manifestBytes = QJsonDocument(published).toJson(QJsonDocument::Indented);
  const TestKey &key = p_options.key ? *p_options.key : trustedKey();
  const QByteArray signature =
      makeSignature(key, manifestBytes, QStringLiteral("VNote %1").arg(p_version).toUtf8());

  QByteArray servedManifest = manifestBytes;
  if (p_options.tamperManifestAfterSigning) {
    // Flip a byte INSIDE a string value so the document still parses; the
    // rejection then has to come from the signature, not from the JSON parser.
    const int at = servedManifest.indexOf("commit-for-");
    QVERIFY(at > 0);
    servedManifest[at] = 'C';
  }

  m_server->serve(assetPath(p_version, manifestAsset(p_version)), servedManifest);
  const QString signaturePath =
      assetPath(p_version, manifestAsset(p_version) + QStringLiteral(".minisig"));
  if (p_options.serveSignature) {
    m_server->serve(signaturePath, signature);
  } else {
    m_server->serveStatus(signaturePath, 404);
  }

  m_releases.insert(p_version, release);
}

void TestUpdateService::setLatestRelease(const QString &p_version) {
  QJsonObject release;
  release[QStringLiteral("tag_name")] = QStringLiteral("v%1").arg(p_version);
  release[QStringLiteral("body")] = QStringLiteral("Release notes for %1").arg(p_version);
  release[QStringLiteral("html_url")] =
      QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v%1").arg(p_version);
  m_server->serve(QStringLiteral("/api/latest"),
                  QJsonDocument(release).toJson(QJsonDocument::Compact));
}

void TestUpdateService::installRelease(const QString &p_version, bool p_withLocalManifest) {
  QVERIFY(m_releases.contains(p_version));
  const Release &release = m_releases.value(p_version);
  for (const auto &entry : release.files) {
    QVERIFY(writeFileAt(m_installDir + QLatin1Char('/') + entry.first, entry.second));
  }
  if (p_withLocalManifest) {
    QVERIFY(writeFileAt(m_installDir + QStringLiteral("/manifest.json"),
                        QJsonDocument(release.core).toJson(QJsonDocument::Indented)));
  }
}

UpdateService *TestUpdateService::makeService(const QString &p_currentVersion) {
  auto *service = new UpdateService(m_installDir, p_currentVersion);
  service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/api/latest")),
                                   m_server->urlFor(QStringLiteral("/download/")));
  service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));
  return service;
}

TestUpdateService::Outcome TestUpdateService::runCheck(UpdateService *p_service, int p_timeoutMs) {
  Outcome outcome;
  QEventLoop loop;

  const auto onFinished = connect(p_service, &UpdateService::checkFinished, &loop,
                                  [&outcome, &loop](const UpdateInfo &p_info) {
                                    outcome.finished = true;
                                    outcome.ok = true;
                                    outcome.info = p_info;
                                    loop.quit();
                                  });
  const auto onFailed =
      connect(p_service, &UpdateService::failed, &loop, [&outcome, &loop](const QString &p_msg) {
        outcome.finished = true;
        outcome.ok = false;
        outcome.error = p_msg;
        loop.quit();
      });

  QTimer::singleShot(p_timeoutMs, &loop, &QEventLoop::quit);
  p_service->checkForUpdates();
  loop.exec();

  QObject::disconnect(onFinished);
  QObject::disconnect(onFailed);
  return outcome;
}

TestUpdateService::Outcome TestUpdateService::runDownload(UpdateService *p_service,
                                                          int p_timeoutMs) {
  Outcome outcome;
  QEventLoop loop;

  const auto onReady = connect(p_service, &UpdateService::readyToApply, &loop,
                               [&outcome, &loop](const QString &p_version) {
                                 outcome.finished = true;
                                 outcome.ok = true;
                                 outcome.version = p_version;
                                 loop.quit();
                               });
  const auto onFailed =
      connect(p_service, &UpdateService::failed, &loop, [&outcome, &loop](const QString &p_msg) {
        outcome.finished = true;
        outcome.ok = false;
        outcome.error = p_msg;
        loop.quit();
      });

  QTimer::singleShot(p_timeoutMs, &loop, &QEventLoop::quit);
  p_service->startDownload();
  loop.exec();

  QObject::disconnect(onReady);
  QObject::disconnect(onFailed);
  return outcome;
}

// ------------------------------------------------------------ setup/teardown

void TestUpdateService::initTestCase() {
#ifndef Q_OS_WIN
  // UpdateService::checkEligibility() returns "in-app updates are only
  // available on Windows" before anything else off Windows, so almost every
  // case here would be asserting behavior the feature never promises. Skipping
  // from initTestCase() skips the whole class while keeping the target in the
  // build on Linux/macOS CI, which still catches compile breakage.
  QSKIP("The incremental updater is Windows x64 only; see the 'Incremental "
        "Update' section of the root AGENTS.md.");
#endif
}

void TestUpdateService::init() {
  m_temp.reset(new QTemporaryDir());
  QVERIFY(m_temp->isValid());

  m_installDir = QDir::cleanPath(m_temp->path() + QStringLiteral("/install"));
  m_scratchDir = QDir::cleanPath(m_temp->path() + QStringLiteral("/scratch"));
  QVERIFY(QDir().mkpath(m_installDir));
  QVERIFY(QDir().mkpath(m_scratchDir));

  m_releases.clear();

  m_server = new TestHttpServer(this);
  QVERIFY2(m_server->start(), qPrintable(m_server->errorString()));

  ManifestSignature::testSetTrustedKeys({publicKeyOf(trustedKey())});
}

void TestUpdateService::cleanup() {
  // The service destructor cancels and WAITS for its worker; it must go before
  // the temp directory it is writing into.
  m_service.reset();

  if (m_server) {
    m_server->close();
    delete m_server;
    m_server = nullptr;
  }

  // Restores the production key list; never leak a test key into another case.
  ManifestSignature::testSetTrustedKeys({});

  m_releases.clear();
  m_temp.reset();
}

// ------------------------------------------------------------ fixture canary

// If this fails, the SIGNER above is broken and every other rejection assertion
// in this file is meaningless (they would all "pass" for the wrong reason).
void TestUpdateService::testSigningFixtureIsAcceptedByTheVerifier() {
  const QByteArray message = QByteArrayLiteral("{\"schema\":1,\"product\":\"VNote\"}");
  const QByteArray signature =
      makeSignature(trustedKey(), message, QByteArrayLiteral("vnote fixture"));

  QString comment;
  const auto verdict = ManifestSignature::verify(message, signature, &comment);
  QVERIFY2(verdict == ManifestSignature::Result::Valid,
           qPrintable(ManifestSignature::resultToString(verdict)));
  QCOMPARE(comment, QStringLiteral("vnote fixture"));

  // And the foreign key really is foreign.
  const auto foreignVerdict = ManifestSignature::verify(
      message, makeSignature(foreignKey(), message, QByteArrayLiteral("nope")));
  QCOMPARE(foreignVerdict, ManifestSignature::Result::UnknownKey);
}

// -------------------------------------------------------- signature gating

void TestUpdateService::testSignedFullPackagePlanProceeds() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY2(outcome.finished, "the check never completed");
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(outcome.info.eligible, qPrintable(outcome.info.ineligibleReason));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.3.1"));
  QCOMPARE(outcome.info.currentVersion, QStringLiteral("4.3.0"));
  QCOMPARE(outcome.info.releaseNotes, QStringLiteral("Release notes for 4.3.1"));
  // No local manifest.json, so the delta path is unavailable by construction.
  QVERIFY(!outcome.info.isDelta);
  QCOMPARE(outcome.info.hopCount, 0);
  QCOMPARE(outcome.info.downloadSize,
           static_cast<qint64>(m_releases[QStringLiteral("4.3.1")].fullZip.size()));
}

void TestUpdateService::testTamperedManifestIsRefused() {
  PublishOptions options;
  options.tamperManifestAfterSigning = true;
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a tampered manifest was accepted");
  QVERIFY2(outcome.error.contains(QStringLiteral("signature verification")),
           qPrintable(outcome.error));
  QVERIFY2(!QFileInfo::exists(stagedDir()), "bytes were staged from a rejected manifest");
}

void TestUpdateService::testUntrustedSigningKeyIsRefused() {
  PublishOptions options;
  options.key = &foreignKey();
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a manifest signed by an untrusted key was accepted");
  QVERIFY2(outcome.error.contains(QStringLiteral("does not trust")), qPrintable(outcome.error));
  QVERIFY(!QFileInfo::exists(stagedDir()));
}

// A missing .minisig must be a REFUSAL, never "well, it is simply unsigned".
void TestUpdateService::testMissingSignatureIsRefused() {
  PublishOptions options;
  options.serveSignature = false;
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "an unsigned manifest was accepted");
  QVERIFY2(outcome.error.contains(QStringLiteral("is not signed")), qPrintable(outcome.error));
  QVERIFY(!QFileInfo::exists(stagedDir()));
}

// EVERY manifest is verified, not just the target: the chain walk decides which
// archives get downloaded and overlaid, so an unverified intermediate would let
// an attacker steer the whole plan. The target here is validly signed; only the
// INTERMEDIATE hop is forged, and the delta path must not be taken.
void TestUpdateService::testForgedIntermediateManifestFallsBackToFullPackage() {
  publish(QStringLiteral("4.3.0"), filesV0());

  PublishOptions forged;
  forged.key = &foreignKey();
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"), forged);

  publish(QStringLiteral("4.3.2"), filesV2(), QStringLiteral("4.3.1"));
  setLatestRelease(QStringLiteral("4.3.2"));

  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.isDelta, "an unverified intermediate hop was trusted");
  QCOMPARE(outcome.info.hopCount, 0);
  QCOMPARE(outcome.info.downloadSize,
           static_cast<qint64>(m_releases[QStringLiteral("4.3.2")].fullZip.size()));
}

// ------------------------------------------------------------ version/replay
// The PUBLISHED BASE is the third manifest fetch site (updateservice.cpp, the
// `publishedBase` block of buildPlan). It is what proves the local tree really
// is the release it claims to be, so an unverified one would let an attacker
// hand VNote a base whose files[] happens to match a tampered install. Here the
// target chain is impeccable and ONLY the base's signature is forged.
void TestUpdateService::testForgedPublishedBaseManifestFallsBackToFullPackage() {
  PublishOptions forgedBase;
  forgedBase.key = &foreignKey();
  publish(QStringLiteral("4.3.0"), filesV0(), QString(), forgedBase);

  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.isDelta, "an unverified published base was trusted");
  QCOMPARE(outcome.info.hopCount, 0);
  QCOMPARE(outcome.info.downloadSize,
           static_cast<qint64>(m_releases[QStringLiteral("4.3.1")].fullZip.size()));
}

// FAIL CLOSED: a build with no signing key cannot authenticate anything, so it
// must report itself ineligible rather than quietly updating unsigned.
//
// Note what is NOT asserted: that no network request happens at all. The
// service deliberately still fetches the release metadata for an ineligible
// install, because UpdateController needs latestVersion/releaseUrl to send the
// user to the download page. What must never happen is a MANIFEST or ARCHIVE
// fetch, and that is what is checked here.
void TestUpdateService::testNoTrustedKeysMakesTheInstallIneligible() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  ManifestSignature::testClearTrustedKeys();
  QVERIFY(!ManifestSignature::hasTrustedKeys());

  m_service.reset(makeService(QStringLiteral("4.3.0")));

  const auto eligibility = m_service->checkEligibility();
  QVERIFY2(!eligibility.eligible, "a build with no signing key reported itself eligible");
  QVERIFY2(eligibility.reason.contains(QStringLiteral("signing key")),
           qPrintable(eligibility.reason));

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QVERIFY2(!outcome.info.eligible, "an unverifiable build was offered an in-place update");
  QVERIFY(!outcome.info.releaseUrl.isEmpty());

  for (const QString &path : m_server->requestedPaths()) {
    QVERIFY2(!path.startsWith(QStringLiteral("/download/")),
             qPrintable(QStringLiteral("an unverifiable build fetched %1").arg(path)));
  }
  QVERIFY(!QFileInfo::exists(stagedDir()));
}

// A correctly signed manifest for a DIFFERENT version must not be accepted in
// place of the requested one; otherwise an old signed release could be replayed
// over a newer install.
void TestUpdateService::testManifestDeclaringAnotherVersionIsRefused() {
  PublishOptions options;
  options.declaredVersion = QStringLiteral("4.2.9");
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a replayed manifest was accepted");
  QVERIFY2(outcome.error.contains(QStringLiteral("declares version")), qPrintable(outcome.error));
}

void TestUpdateService::testOlderOrEqualReleaseOffersNoUpdate() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.1")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(!outcome.info.updateAvailable);
  QCOMPARE(outcome.info.downloadSize, static_cast<qint64>(0));
  // The manifest was never even fetched: there is nothing to plan.
  QVERIFY(!m_server->requestedPaths().contains(
      assetPath(QStringLiteral("4.3.1"), manifestAsset(QStringLiteral("4.3.1")))));
}

// ----------------------------------------------------------------- planning

void TestUpdateService::testDeltaSingleHopIsPlanned() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(outcome.info.isDelta, "the delta path was not taken");
  QCOMPARE(outcome.info.hopCount, 1);
  QCOMPARE(outcome.info.downloadSize,
           static_cast<qint64>(m_releases[QStringLiteral("4.3.1")].deltaZip.size()));
  QVERIFY2(outcome.info.downloadSize <
               static_cast<qint64>(m_releases[QStringLiteral("4.3.1")].fullZip.size()),
           "the delta is not actually smaller than the full package");
}

void TestUpdateService::testNoLocalManifestFallsBackToFullPackage() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  setLatestRelease(QStringLiteral("4.3.1"));
  // Files on disk, but no manifest.json: an install from before the updater.
  installRelease(QStringLiteral("4.3.0"), false);

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(!outcome.info.isDelta);
}

// A continuous build's contents are not reproducible from a published tag, so it
// is never a legal delta base even when everything else lines up.
void TestUpdateService::testContinuousLocalChannelFallsBackToFullPackage() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  // Rewrite the LOCAL manifest onto the continuous channel.
  QJsonObject local = m_releases[QStringLiteral("4.3.0")].core;
  local[QStringLiteral("channel")] = QStringLiteral("continuous");
  QVERIFY(writeFileAt(m_installDir + QStringLiteral("/manifest.json"),
                      QJsonDocument(local).toJson(QJsonDocument::Indented)));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.isDelta, "a continuous install was used as a delta base");
}

// Any drift from the verified base makes the delta unusable: a locally patched
// file would be silently preserved into the new version.
void TestUpdateService::testLocalDriftFallsBackToFullPackage() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  // A file that the delta does NOT touch, so only the drift check can catch it.
  QVERIFY(
      writeFileAt(m_installDir + QStringLiteral("/Qt6Gui.dll"), blobOf("QTGUI-locally-patched-")));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.isDelta, "a drifted install was used as a delta base");
  QCOMPARE(outcome.info.downloadSize,
           static_cast<qint64>(m_releases[QStringLiteral("4.3.1")].fullZip.size()));
}

// Past UpdateManifest::c_maxChainSizeRatio of the target's expanded size the
// delta stops being worth its risk and the full package is used instead.
void TestUpdateService::testOversizedChainFallsBackToFullPackage() {
  publish(QStringLiteral("4.3.0"), filesV0());

  const qint64 expanded = 5 * blobOf("EXE-v1-").size(); // comfortably over the cap
  PublishOptions options;
  options.declaredDeltaSize = expanded * 4;
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"), options);
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.isDelta, "an oversized chain was accepted");
}

// ------------------------------------------------------------------ staging

// The load-bearing staging test. 4.3.0 -> 4.3.1 -> 4.3.2 exercises, in one run:
//   * hops applied OLDEST FIRST (vnote.exe changes in both; only the newest
//     content can survive);
//   * the REVERT case (Qt6Core.dll changes in 4.3.1 and returns to its 4.3.0
//     bytes in 4.3.2, so it must not be staged at all);
//   * the INTERMEDIATE-ONLY case (plugins/extra.dll exists only in 4.3.1 and is
//     pruned from staged/);
//   * derived deletions (obsolete.dll);
//   * manifest.json never surviving in staged/.
void TestUpdateService::testDeltaStagingAppliesHopsOldestFirstAndPrunes() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"));
  publish(QStringLiteral("4.3.2"), filesV2(), QStringLiteral("4.3.1"));
  setLatestRelease(QStringLiteral("4.3.2"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));

  const Outcome check = runCheck(m_service.data());
  QVERIFY2(check.ok, qPrintable(check.error));
  QVERIFY2(check.info.isDelta, "the delta path was not taken");
  QCOMPARE(check.info.hopCount, 2);

  m_server->clearRequests();
  const Outcome download = runDownload(m_service.data());
  QVERIFY2(download.ok, qPrintable(download.error));
  QCOMPARE(download.version, QStringLiteral("4.3.2"));

  // EXACTLY the two delta archives, oldest hop first, and nothing else. An
  // exact ordered comparison is what makes this an assertion about the plan
  // rather than about mere presence: a redundant refetch, or the full package
  // sneaking in alongside the deltas, has to fail here.
  QStringList expectedRequests;
  expectedRequests.append(assetPath(QStringLiteral("4.3.1"), deltaAsset(QStringLiteral("4.3.1"))));
  expectedRequests.append(assetPath(QStringLiteral("4.3.2"), deltaAsset(QStringLiteral("4.3.2"))));
  QCOMPARE(m_server->requestedPaths(), expectedRequests);

  // expectedChanged(4.3.0, 4.3.2) is exactly {vnote.exe}: everything else is
  // either untouched, reverted, or intermediate-only.
  QCOMPARE(relativeFilesUnder(stagedDir()), QStringList{QStringLiteral("vnote.exe")});
  QCOMPARE(readFileAt(stagedDir() + QStringLiteral("/vnote.exe")), blobOf("EXE-v2-"));

  const auto pending = UpdateInstaller::readPending(m_installDir);
  QVERIFY(pending.isValid());
  QCOMPARE(pending.targetVersion, QStringLiteral("4.3.2"));
  QCOMPARE(pending.variant, variant());
  QCOMPARE(pending.staged, QStringList{QStringLiteral("vnote.exe")});
  QCOMPARE(pending.deletions, QStringList{QStringLiteral("obsolete.dll")});
  QCOMPARE(pending.executablePath, QFileInfo(QCoreApplication::applicationFilePath()).fileName());

  // The installer commits manifest.json itself; it must never be part of the
  // staged set that the swap moves.
  QVERIFY(!QFileInfo::exists(stagedDir() + QStringLiteral("/manifest.json")));
  QVERIFY(!pending.staged.contains(QStringLiteral("manifest.json")));

  QVERIFY2(m_service->revalidatePending(), "a freshly staged plan did not revalidate");
}

void TestUpdateService::testHopArchiveWithAnExtraEntryIsRejected() {
  publish(QStringLiteral("4.3.0"), filesV0());

  PublishOptions options;
  options.extraDeltaEntries.append(qMakePair(QStringLiteral("smuggled.dll"), blobOf("SMUGGLED-")));
  publish(QStringLiteral("4.3.1"), filesV1(), QStringLiteral("4.3.0"), options);
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome check = runCheck(m_service.data());
  QVERIFY2(check.ok, qPrintable(check.error));
  QVERIFY(check.info.isDelta);

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "a delta archive with an unexpected entry was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("was rejected")), qPrintable(download.error));
  QVERIFY(!QFileInfo::exists(stagedDir() + QStringLiteral("/smuggled.dll")));
}

// There is no pruning on the full-package path: the archive is supposed to equal
// the target manifest exactly, so an extra entry means it is not what it claims.
void TestUpdateService::testFullPackageWithAnUnexpectedEntryIsRejected() {
  PublishOptions options;
  options.extraFullEntries.append(qMakePair(QStringLiteral("stowaway.dll"), blobOf("STOWAWAY-")));
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "an unexpected file in the full package was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("unexpected file")), qPrintable(download.error));
}

void TestUpdateService::testFullPackageWithoutAnInPackageManifestIsRejected() {
  PublishOptions options;
  options.omitInPackageManifest = true;
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "a full package without manifest.json was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("does not contain a manifest")),
           qPrintable(download.error));
}

// The in-package manifest becomes the next delta BASE. If it disagreed with the
// bytes actually shipped, every later incremental update would be poisoned.
// Two independent guards exist; each gets its own case.
//
// Guard 1 -- IDENTITY. Everything about which release this is. Only `commit` is
// changed here, so the complete-file-map comparison below cannot be what fires.
void TestUpdateService::testFullPackageIdentityMismatchIsRejected() {
  PublishOptions options;
  options.identityMismatchInPackageManifest = true;
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "a package manifest with a foreign identity was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("does not match the published manifest")),
           qPrintable(download.error));
  QVERIFY(!UpdateInstaller::readPending(m_installDir).isValid());
}

// Guard 2 -- the complete files[] map. Identity is left intact, so only the
// per-file comparison can reject this package.
void TestUpdateService::testFullPackageManifestDisagreementIsRejected() {
  PublishOptions options;
  options.disagreeingInPackageManifest = true;
  publish(QStringLiteral("4.3.1"), filesV1(), QString(), options);
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "a package manifest disagreeing with the published one was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("disagrees with the published manifest")),
           qPrintable(download.error));
  QVERIFY(!UpdateInstaller::readPending(m_installDir).isValid());
}

void TestUpdateService::testFullPackageStagingProducesAUsablePendingPlan() {
  publish(QStringLiteral("4.3.0"), filesV0());
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));
  installRelease(QStringLiteral("4.3.0"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome check = runCheck(m_service.data());
  QVERIFY2(check.ok, qPrintable(check.error));
  QVERIFY(!check.info.isDelta); // 4.3.1 publishes no delta at all

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(download.ok, qPrintable(download.error));

  // The full package stages EVERY file of the target.
  QStringList expected;
  for (const auto &entry : filesV1()) {
    expected.append(entry.first);
  }
  expected.sort();
  QCOMPARE(relativeFilesUnder(stagedDir()), expected);
  QCOMPARE(readFileAt(stagedDir() + QStringLiteral("/plugins/extra.dll")), blobOf("EXTRA-v1-"));
  QVERIFY(!QFileInfo::exists(stagedDir() + QStringLiteral("/manifest.json")));

  const auto pending = UpdateInstaller::readPending(m_installDir);
  QVERIFY(pending.isValid());
  QCOMPARE(pending.staged, expected);
  QCOMPARE(pending.deletions, QStringList{QStringLiteral("obsolete.dll")});

  // The archives are dropped once everything is staged and verified.
  QVERIFY(!QFileInfo::exists(UpdateInstaller::downloadDir(m_installDir)));
}

// -------------------------------------------------------- pending lifecycle

// revalidatePending() is a pure disk-consistency check, so these cases stage a
// plan BY HAND. Driving each one through a full download would only make them
// slower and less specific about which rule did the rejecting.
//
// Every plan below carries a non-empty staged set on purpose: PendingPlan
// requires one, and an empty plan would be discarded as structurally invalid --
// passing the test for a reason that has nothing to do with what it claims to
// check.

void TestUpdateService::testRevalidatePendingAcceptsAConsistentPlan() {
  publish(QStringLiteral("4.3.1"), filesV1());

  const Release &release = m_releases[QStringLiteral("4.3.1")];
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.1");
  plan.variant = variant();
  plan.executablePath = QStringLiteral("vnote.exe");
  plan.targetManifest = release.core;
  for (const auto &entry : release.files) {
    plan.staged.append(entry.first);
    QVERIFY(writeFileAt(stagedDir() + QLatin1Char('/') + entry.first, entry.second));
  }
  plan.staged.sort();
  QVERIFY(UpdateInstaller::writePending(m_installDir, plan));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(m_service->revalidatePending());
  QVERIFY(m_service->hasPendingUpdate());
  QCOMPARE(m_service->pendingVersion(), QStringLiteral("4.3.1"));
}

void TestUpdateService::testRevalidatePendingDiscardsAForeignVariant() {
  publish(QStringLiteral("4.3.1"), filesV1());

  const Release &release = m_releases[QStringLiteral("4.3.1")];
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.1");
  plan.variant = QStringLiteral("linux-x64-not-this-build");
  plan.executablePath = QStringLiteral("vnote.exe");
  plan.targetManifest = release.core;
  for (const auto &entry : release.files) {
    plan.staged.append(entry.first);
    QVERIFY(writeFileAt(stagedDir() + QLatin1Char('/') + entry.first, entry.second));
  }
  plan.staged.sort();
  QVERIFY(UpdateInstaller::writePending(m_installDir, plan));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY2(!m_service->revalidatePending(), "a foreign-variant plan survived revalidation");
  QVERIFY(!m_service->hasPendingUpdate());
}

// The user manually installed 4.3.1 (or newer) while the plan sat on disk.
void TestUpdateService::testRevalidatePendingDiscardsAnAlreadyAppliedTarget() {
  publish(QStringLiteral("4.3.1"), filesV1());

  const Release &release = m_releases[QStringLiteral("4.3.1")];
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.1");
  plan.variant = variant();
  plan.executablePath = QStringLiteral("vnote.exe");
  plan.targetManifest = release.core;
  for (const auto &entry : release.files) {
    plan.staged.append(entry.first);
    QVERIFY(writeFileAt(stagedDir() + QLatin1Char('/') + entry.first, entry.second));
  }
  plan.staged.sort();
  QVERIFY(UpdateInstaller::writePending(m_installDir, plan));

  m_service.reset(makeService(QStringLiteral("4.3.1")));
  QVERIFY2(!m_service->revalidatePending(), "a superseded plan survived revalidation");
  QVERIFY(!m_service->hasPendingUpdate());
}

void TestUpdateService::testRevalidatePendingDiscardsATamperedStagedFile() {
  publish(QStringLiteral("4.3.1"), filesV1());

  const Release &release = m_releases[QStringLiteral("4.3.1")];
  UpdateInstaller::PendingPlan plan;
  plan.targetVersion = QStringLiteral("4.3.1");
  plan.variant = variant();
  plan.executablePath = QStringLiteral("vnote.exe");
  plan.targetManifest = release.core;
  for (const auto &entry : release.files) {
    plan.staged.append(entry.first);
    QVERIFY(writeFileAt(stagedDir() + QLatin1Char('/') + entry.first, entry.second));
  }
  plan.staged.sort();
  QVERIFY(UpdateInstaller::writePending(m_installDir, plan));

  // Same length, different bytes: only the hash can catch this.
  QByteArray tampered = release.files.at(0).second;
  tampered[0] = tampered.at(0) == 'X' ? 'Y' : 'X';
  QVERIFY(writeFileAt(stagedDir() + QLatin1Char('/') + release.files.at(0).first, tampered));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY2(!m_service->revalidatePending(), "a tampered staged file survived revalidation");
  QVERIFY(!m_service->hasPendingUpdate());
  QVERIFY(!QFileInfo::exists(stagedDir()));
}

// result.json is how an apply outcome crosses the restart; NotificationService
// is in-memory only and cannot.
void TestUpdateService::testConsumeStoredResultReadsAndClears() {
  QVERIFY(UpdateInstaller::writeResult(m_installDir, UpdateInstaller::ResultOutcome::Applied,
                                       QStringLiteral("updated"), QStringLiteral("all good"),
                                       QStringLiteral("4.3.1")));

  m_service.reset(makeService(QStringLiteral("4.3.1")));
  const auto stored = m_service->consumeStoredResult();
  QVERIFY(stored.isValid());
  QCOMPARE(stored.outcome, UpdateInstaller::ResultOutcome::Applied);
  QCOMPARE(stored.targetVersion, QStringLiteral("4.3.1"));
  QCOMPARE(stored.reason, QStringLiteral("updated"));

  // Consumed exactly once.
  QVERIFY(!m_service->consumeStoredResult().isValid());
  QVERIFY(!QFileInfo::exists(UpdateInstaller::resultPath(m_installDir)));
}

// --------------------------------------------------------------- networking

void TestUpdateService::testRedirectWithinTheAllowlistIsFollowed() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  // Redirect the API entry point rather than an asset, so the manifest keeps
  // being served from its canonical route and its signature stays meaningful.
  m_server->serveRedirect(QStringLiteral("/api/latest-redirect"), QStringLiteral("/api/latest"));

  m_service.reset(new UpdateService(m_installDir, QStringLiteral("4.3.0")));
  m_service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/api/latest-redirect")),
                                     m_server->urlFor(QStringLiteral("/download/")));
  m_service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.3.1"));
  QVERIFY(m_server->requestedPaths().contains(QStringLiteral("/api/latest-redirect")));
  QVERIFY2(m_server->requestedPaths().contains(QStringLiteral("/api/latest")),
           "the redirect was not followed");
}

// Every hop is checked, not just the first URL: a redirect is an attacker's
// cheapest way to move a download somewhere else.
void TestUpdateService::testRedirectToAnUnexpectedHostIsRefused() {
  publish(QStringLiteral("4.3.1"), filesV1());
  m_server->serveRedirect(QStringLiteral("/api/latest"),
                          QStringLiteral("http://not-allowed.invalid/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a redirect to an unexpected host was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
  QVERIFY2(!m_server->requestedPaths().contains(QStringLiteral("/releases/latest")),
           "the redirect target was contacted");
}

// Isolates the SCHEME rule from the HOST rule. The redirect target here is
// api.github.com -- a production-allowlisted host -- over plain HTTP, so the
// only thing that can reject it is the no-downgrade predicate. Without this
// case, testRedirectToAnUnexpectedHostIsRefused above would keep passing even
// if plain HTTP were quietly allowed for GitHub itself.
//
// Nothing is ever sent to api.github.com: the check happens before the request.
void TestUpdateService::testRedirectDowngradingToPlainHttpIsRefused() {
  publish(QStringLiteral("4.3.1"), filesV1());
  const QString downgraded =
      QStringLiteral("http://api.github.com/repos/vnotex/vnote/releases/latest");
  m_server->serveRedirect(QStringLiteral("/api/latest"), downgraded);

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "an HTTPS -> HTTP downgrade was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testTooManyRedirectsAreRefused() {
  // One more hop than UpdateService::c_maxRedirects.
  const int hops = UpdateService::c_maxRedirects + 2;
  for (int i = 0; i < hops; ++i) {
    m_server->serveRedirect(QStringLiteral("/hop%1").arg(i), QStringLiteral("/hop%1").arg(i + 1));
  }
  m_server->serve(QStringLiteral("/hop%1").arg(hops), QByteArrayLiteral("{}"));

  m_service.reset(new UpdateService(m_installDir, QStringLiteral("4.3.0")));
  m_service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/hop0")),
                                     m_server->urlFor(QStringLiteral("/download/")));
  m_service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "an unbounded redirect chain was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("Too many redirects")), qPrintable(outcome.error));
}

void TestUpdateService::testDownloadLargerThanDeclaredIsAborted() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  // Serve MORE bytes than the (signed) manifest declares. The manifest is not
  // touched, so this is exactly the "server lies about the body" case.
  const QString route =
      assetPath(QStringLiteral("4.3.1"), fullPackageAsset(QStringLiteral("4.3.1")));
  m_server->serve(route, m_releases[QStringLiteral("4.3.1")].fullZip + QByteArray(8192, 'x'));

  const Outcome download = runDownload(m_service.data());
  QVERIFY2(!download.ok, "an oversized download was accepted");
  QVERIFY2(download.error.contains(QStringLiteral("larger than the manifest declared")),
           qPrintable(download.error));
}

// Cancellation is POLLED every 250 ms inside the blocking request, so it must
// return in well under the 600 s download timeout. Without the poll, quitting
// VNote during a stalled download would block teardown (and therefore any
// pending update) for the whole timeout.
//
// cancel() is fired from the server's own requestReceived signal rather than
// after a fixed delay, so the archive request is provably IN FLIGHT when it
// happens. A timed cancel could land before the worker even reached
// downloadToFile(), in which case the top-of-loop isCancelled() check would end
// the download and the test would pass with the poll removed entirely.
void TestUpdateService::testCancelDuringDownloadReturnsPromptly() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QVERIFY(runCheck(m_service.data()).ok);

  // The archive request now never gets an answer within the test's lifetime.
  const QString route =
      assetPath(QStringLiteral("4.3.1"), fullPackageAsset(QStringLiteral("4.3.1")));
  m_server->setDelay(route, 120000);

  Outcome outcome;
  QEventLoop loop;
  const auto onReady =
      connect(m_service.data(), &UpdateService::readyToApply, &loop, [&outcome, &loop]() {
        outcome.finished = true;
        outcome.ok = true;
        loop.quit();
      });
  const auto onFailed = connect(m_service.data(), &UpdateService::failed, &loop,
                                [&outcome, &loop](const QString &p_msg) {
                                  outcome.finished = true;
                                  outcome.ok = false;
                                  outcome.error = p_msg;
                                  loop.quit();
                                });

  QElapsedTimer sinceRequest;
  bool requestSeen = false;
  const auto onRequest = connect(m_server, &TestHttpServer::requestReceived, &loop,
                                 [this, route, &sinceRequest, &requestSeen](const QString &p_path) {
                                   if (requestSeen || p_path != route) {
                                     return;
                                   }
                                   requestSeen = true;
                                   sinceRequest.start();
                                   m_service->cancel();
                                 });

  m_service->startDownload();
  QTimer::singleShot(20000, &loop, &QEventLoop::quit);
  loop.exec();

  QObject::disconnect(onReady);
  QObject::disconnect(onFailed);
  QObject::disconnect(onRequest);

  QVERIFY2(requestSeen, "the archive download never reached the server");
  QVERIFY2(outcome.finished, "cancel() did not unblock the download");
  QVERIFY2(!outcome.ok, "a cancelled download reported success");
  QVERIFY2(outcome.error.contains(QStringLiteral("Cancelled")), qPrintable(outcome.error));
  QVERIFY2(sinceRequest.elapsed() < 10000,
           qPrintable(QStringLiteral("cancellation took %1 ms after the request went out")
                          .arg(sinceRequest.elapsed())));
}

// ------------------------------------------------------------ release source

void TestUpdateService::testSourceStringRoundTrip() {
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("gitee")), UpdateService::Source::Gitee);
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("GiTee")), UpdateService::Source::Gitee);
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("github")),
           UpdateService::Source::GitHub);
  // Anything unrecognized falls back to GitHub rather than to "no source".
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("gitlab")),
           UpdateService::Source::GitHub);
  QCOMPARE(UpdateService::sourceFromString(QString()), UpdateService::Source::GitHub);

  QCOMPARE(UpdateService::sourceToString(UpdateService::Source::Gitee), QStringLiteral("gitee"));
  QCOMPARE(UpdateService::sourceToString(UpdateService::Source::GitHub), QStringLiteral("github"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QCOMPARE(m_service->source(), UpdateService::Source::GitHub);
  m_service->setSource(UpdateService::Source::Gitee);
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);
}

// The two allowlists are disjoint: a client pointed at one forge must not even
// know the other forge's host names.
void TestUpdateService::testAllowlistIsPerSource() {
  const QStringList github = UpdateService::allowedHosts(UpdateService::Source::GitHub);
  const QStringList gitee = UpdateService::allowedHosts(UpdateService::Source::Gitee);

  QVERIFY(github.contains(QStringLiteral("api.github.com")));
  QVERIFY(github.contains(QStringLiteral("github.com")));
  QVERIFY(!github.contains(QStringLiteral("gitee.com")));

  QVERIFY(gitee.contains(QStringLiteral("gitee.com")));
  QVERIFY(!gitee.contains(QStringLiteral("github.com")));
  QVERIFY(!gitee.contains(QStringLiteral("api.github.com")));
}

// A cross-forge redirect is the cheapest way to move a download somewhere the
// user did not choose, so it is refused BEFORE the request goes out. Both
// directions get a case; a one-sided check would pass even if one allowlist
// silently contained the other's hosts.
void TestUpdateService::testGiteeRedirectToGitHubHostsIsRefused() {
  publish(QStringLiteral("4.3.1"), filesV1());
  m_server->serveRedirect(
      QStringLiteral("/api/latest"),
      QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  m_service->setSource(UpdateService::Source::Gitee);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a Gitee client followed a redirect to a GitHub host");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testGitHubRedirectToGiteeHostsIsRefused() {
  publish(QStringLiteral("4.3.1"), filesV1());
  m_server->serveRedirect(QStringLiteral("/api/latest"),
                          QStringLiteral("https://gitee.com/vnotex/vnote/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  QCOMPARE(m_service->source(), UpdateService::Source::GitHub);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a GitHub client followed a redirect to a Gitee host");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testReleasesPageUrlFollowsTheSource() {
  m_service.reset(makeService(QStringLiteral("4.3.0")));

  QCOMPARE(m_service->releasesPageUrl().toString(),
           QStringLiteral("https://github.com/vnotex/vnote/releases"));

  m_service->setSource(UpdateService::Source::Gitee);
  QCOMPARE(m_service->releasesPageUrl().toString(),
           QStringLiteral("https://gitee.com/vnotex/vnote/releases"));
}

// Gitee's release JSON carries no html_url, so the page URL has to be
// synthesized from the tag. The API fixture here DOES serve an html_url; a
// Gitee client must ignore it rather than send the user to github.com.
//
// The exact shape is pinned on purpose: it was VERIFIED against the live
// https://gitee.com/vnotex/vnote/releases/tag/v4.3.0 page. Note the `tag/`
// segment -- the asset download path (/releases/download/v<ver>/...) does not
// have it, and an earlier version of this code wrongly reused that shape.
void TestUpdateService::testGiteeSynthesizesTheReleasePageUrl() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  m_service->setSource(UpdateService::Source::Gitee);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.releaseUrl,
           QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.3.1"));

  // The GitHub path keeps using the API's own html_url.
  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome github = runCheck(m_service.data());
  QVERIFY2(github.ok, qPrintable(github.error));
  QCOMPARE(github.info.releaseUrl,
           QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v4.3.1"));
}

// D10. A source that mirrors the release object but not the update assets
// answers 404 for the MANIFEST. That is a degradation, not an attack: report
// the update as available but ineligible, so the caller can offer the release
// page. Reporting failed() instead would surface as "could not check for
// updates", which is both wrong and unactionable.
//
// This is deliberately narrow: testMissingSignatureIsRefused() covers the case
// where the manifest DOES arrive and only the .minisig 404s, which stays a hard
// refusal. Absence is only ever inferred from the manifest fetch itself.
void TestUpdateService::testMissingManifestDegradesToCheckOnly() {
  // No publish() at all: the manifest route falls through to the server's 404.
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(
      outcome.ok,
      qPrintable(QStringLiteral("a missing manifest reported failure: %1").arg(outcome.error)));
  QVERIFY2(outcome.info.updateAvailable, "the available update was hidden");
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.3.1"));
  QVERIFY2(!outcome.info.eligible, "an unplannable update was offered in-place");
  QVERIFY2(outcome.info.ineligibleReason.contains(QStringLiteral("release page")),
           qPrintable(outcome.info.ineligibleReason));
  QVERIFY(!outcome.info.releaseUrl.isEmpty());

  // Nothing was planned and nothing was staged.
  QCOMPARE(outcome.info.downloadSize, static_cast<qint64>(0));
  QVERIFY(!QFileInfo::exists(stagedDir()));

  // The manifest WAS attempted; the signature was not, because there is nothing
  // to verify.
  const QStringList requested = m_server->requestedPaths();
  QVERIFY(requested.contains(
      assetPath(QStringLiteral("4.3.1"), manifestAsset(QStringLiteral("4.3.1")))));
  QVERIFY(!requested.contains(
      assetPath(QStringLiteral("4.3.1"),
                manifestAsset(QStringLiteral("4.3.1")) + QStringLiteral(".minisig"))));
}

// ------------------------------------------------------------- eligibility

// An MSIX install lives under C:\Program Files\WindowsApps, so the Store gate
// must fire BEFORE the Program Files check -- otherwise the user is told to
// "use the installer package", which does not exist for a Store install.
void TestUpdateService::testPackagedAppIsIneligibleWithAStoreReason() {
  m_service.reset(makeService(QStringLiteral("4.3.0")));

  // Baseline: this temp install dir is eligible on a normal build.
  m_service->testSetPackagedAppOverride(0);
  const auto unpackaged = m_service->checkEligibility();
  QVERIFY2(unpackaged.eligible, qPrintable(unpackaged.reason));

  m_service->testSetPackagedAppOverride(1);
  const auto packaged = m_service->checkEligibility();
  QVERIFY2(!packaged.eligible, "a Microsoft Store install reported itself eligible");
  QVERIFY2(packaged.reason.contains(QStringLiteral("Microsoft Store")),
           qPrintable(packaged.reason));
  QVERIFY2(!packaged.reason.contains(QStringLiteral("Program Files")),
           qPrintable(QStringLiteral("the Store gate ran after the Program Files gate: %1")
                          .arg(packaged.reason)));

  // -1 restores auto-detection; the test process is not packaged.
  m_service->testSetPackagedAppOverride(-1);
  QVERIFY(m_service->checkEligibility().eligible);
}

// -------------------------------------------------- startDownload contract
//
// UpdateController keys which UI surface owns a transfer off this return value.
// Getting it wrong either strands that surface forever (a refusal that emits no
// terminal signal) or routes one transfer's result into another's widgets, so
// each outcome gets a case.

void TestUpdateService::testStartDownloadWithoutAPlanReportsNoPlan() {
  m_service.reset(makeService(QStringLiteral("4.3.0")));

  // NoPlan DOES own a terminal signal: the caller must claim its surface to
  // receive the failed() that follows.
  QSignalSpy failures(m_service.data(), &UpdateService::failed);
  QCOMPARE(m_service->startDownload(), UpdateService::DownloadStart::NoPlan);
  QVERIFY(failures.wait(5000));
  QCOMPARE(failures.size(), 1);
  QVERIFY2(failures.at(0).at(0).toString().contains(QStringLiteral("planned")),
           qPrintable(failures.at(0).at(0).toString()));

  // The busy flag was released again, so a later legitimate download still runs.
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));
  QVERIFY(runCheck(m_service.data()).ok);
  QCOMPARE(m_service->startDownload(QStringLiteral("4.3.1")),
           UpdateService::DownloadStart::Started);
}

// A notification or a non-modal dialog can outlive the check it came from. Its
// Update button must NOT download whatever plan the service holds now while
// still advertising the old version.
void TestUpdateService::testStartDownloadRefusesAStaleExpectedVersion() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));

  m_service.reset(makeService(QStringLiteral("4.3.0")));
  const Outcome check = runCheck(m_service.data());
  QVERIFY2(check.ok, qPrintable(check.error));
  QCOMPARE(check.info.latestVersion, QStringLiteral("4.3.1"));

  QSignalSpy failures(m_service.data(), &UpdateService::failed);

  // Stale starts nothing AND emits nothing: the caller must not claim a surface.
  QCOMPARE(m_service->startDownload(QStringLiteral("4.2.9")), UpdateService::DownloadStart::Stale);
  QVERIFY2(!failures.wait(1000), "a stale refusal emitted a terminal signal");
  QVERIFY(!QFileInfo::exists(stagedDir()));

  // The matching version is still accepted afterwards; Stale released the flag.
  QCOMPARE(m_service->startDownload(QStringLiteral("4.3.1")),
           UpdateService::DownloadStart::Started);
}

// Busy must win over NoPlan. A download requested mid-check would otherwise read
// m_plan while the check worker is assigning it, and would answer NoPlan --
// which emits failed() and hands the caller ownership of a terminal signal that
// belongs to the check.
void TestUpdateService::testStartDownloadIsBusyWhileACheckIsRunning() {
  publish(QStringLiteral("4.3.1"), filesV1());
  setLatestRelease(QStringLiteral("4.3.1"));
  // Stall the API response so the check is provably still in flight below.
  m_server->setDelay(QStringLiteral("/api/latest"), 3000);

  m_service.reset(makeService(QStringLiteral("4.3.0")));

  QEventLoop loop;
  bool finished = false;
  const auto onFinished = connect(m_service.data(), &UpdateService::checkFinished, &loop,
                                  [&finished, &loop](const UpdateInfo &) {
                                    finished = true;
                                    loop.quit();
                                  });
  const auto onFailed = connect(m_service.data(), &UpdateService::failed, &loop,
                                [&loop](const QString &) { loop.quit(); });

  UpdateService::DownloadStart duringCheck = UpdateService::DownloadStart::Started;
  QTimer::singleShot(500, m_service.data(), [this, &duringCheck]() {
    duringCheck = m_service->startDownload(QStringLiteral("4.3.1"));
  });

  QTimer::singleShot(20000, &loop, &QEventLoop::quit);
  m_service->checkForUpdates();
  loop.exec();

  QObject::disconnect(onFinished);
  QObject::disconnect(onFailed);

  QVERIFY2(finished, "the check never completed");
  QCOMPARE(duringCheck, UpdateService::DownloadStart::Busy);
  QVERIFY2(!QFileInfo::exists(stagedDir()), "a refused download staged bytes");
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUpdateService)
#include "test_updateservice.moc"
