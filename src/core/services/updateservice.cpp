#include "updateservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTimer>
#include <QVersionNumber>
#include <QtConcurrent>

#include <functional>

#include <core/manifestsignature.h>
#include <core/zipextractor.h>

using namespace vnotex;

constexpr int UpdateService::c_maxRedirects;
constexpr qint64 UpdateService::c_maxArchiveBytes;
constexpr qint64 UpdateService::c_freeSpaceHeadroom;

namespace {

const QString c_repoOwner = QStringLiteral("vnotex");
const QString c_repoName = QStringLiteral("vnote");

const QString c_githubApiLatestUrl =
    QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest");

const QString c_githubReleaseDownloadBase =
    QStringLiteral("https://github.com/vnotex/vnote/releases/download");

const QString c_githubReleasesPageUrl = QStringLiteral("https://github.com/vnotex/vnote/releases");

// Gitee's asset URLs have the same deterministic shape as GitHub's; the
// download endpoint redirects twice (attach_files -> foruda.gitee.com), which
// is well inside c_maxRedirects.
const QString c_giteeApiLatestUrl =
    QStringLiteral("https://gitee.com/api/v5/repos/vnotex/vnote/releases/latest");

const QString c_giteeReleaseDownloadBase =
    QStringLiteral("https://gitee.com/vnotex/vnote/releases/download");

const QString c_giteeReleasesPageUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases");

// Overall request timeout. QNetworkAccessManager's own transfer timeout only
// covers stalls, not a slow-but-alive server.
constexpr int c_requestTimeoutMs = 60 * 1000;

// How often a blocking request re-checks the cancellation flag. Bounds how long
// service teardown can wait on an in-flight transfer.
constexpr int c_cancelPollMs = 250;

QString hashFileSha256(const QString &p_path) { return UpdateInstaller::hashFile(p_path); }

} // namespace

const QStringList &UpdateService::allowedHosts(Source p_source) {
  // GitHub redirects release assets to *.githubusercontent.com. The exact
  // subdomain has moved over time (objects. -> release-assets.), so the suffix
  // is allowlisted rather than a fixed host (see isHostAllowed), while
  // api./github.com stay exact.
  static const QStringList githubHosts{QStringLiteral("api.github.com"),
                                       QStringLiteral("github.com"),
                                       QStringLiteral("codeload.github.com")};
  // Gitee redirects release assets to foruda.gitee.com, covered by the
  // ".gitee.com" suffix rule in isHostAllowed.
  static const QStringList giteeHosts{QStringLiteral("gitee.com")};

  return p_source == Source::Gitee ? giteeHosts : githubHosts;
}

UpdateService::Source UpdateService::sourceFromString(const QString &p_source) {
  return p_source.trimmed().compare(QLatin1String("gitee"), Qt::CaseInsensitive) == 0
             ? Source::Gitee
             : Source::GitHub;
}

QString UpdateService::sourceToString(Source p_source) {
  return p_source == Source::Gitee ? QStringLiteral("gitee") : QStringLiteral("github");
}

void UpdateService::setSource(Source p_source) {
  if (m_source == p_source) {
    return;
  }
  if (m_busy.load()) {
    // Switching origins mid-flight would let one plan mix manifests and
    // archives from two different servers.
    qWarning() << "update: ignoring a source change to" << sourceToString(p_source)
               << "while a check or download is in flight";
    return;
  }
  m_source = p_source;
  // The plan was built against the previous source; nothing about it is valid
  // for the new one.
  m_plan = Plan();
}

QUrl UpdateService::releasesPageUrl() const {
  return QUrl(m_source == Source::Gitee ? c_giteeReleasesPageUrl : c_githubReleasesPageUrl);
}

UpdateService::UpdateService(const QString &p_installDir, const QString &p_currentVersion,
                             QObject *p_parent)
    : QObject(p_parent), m_installDir(QDir::cleanPath(p_installDir)),
      m_currentVersion(p_currentVersion) {
  qRegisterMetaType<vnotex::UpdateInfo>("vnotex::UpdateInfo");
}

UpdateService::~UpdateService() {
  cancel();
  // A worker holds a raw `this`; it must not outlive the object.
  if (m_worker.isRunning()) {
    m_worker.waitForFinished();
  }
}

// ===========================================================================
// Eligibility
// ===========================================================================

UpdateService::Eligibility UpdateService::checkEligibility(bool p_forceProbe) const {
  Eligibility result;

#ifndef Q_OS_WIN
  result.reason = tr("In-app updates are only available on Windows.");
  return result;
#else
  if (QSysInfo::WordSize != 64) {
    result.reason = tr("In-app updates require a 64-bit build.");
    return result;
  }

  if (m_installDir.isEmpty() || !QFileInfo(m_installDir).isDir()) {
    result.reason = tr("The installation directory could not be determined.");
    return result;
  }

  // Ordering matters: an MSIX install lives under
  // C:\Program Files\WindowsApps, so the Store gate must run BEFORE the
  // Program Files check or the user is told to "use the installer package",
  // which does not exist for a Store install.
  const bool packaged = m_packagedAppOverride >= 0 ? m_packagedAppOverride != 0
                                                   : UpdateInstaller::isMicrosoftStoreInstall();
  if (packaged) {
    result.reason = tr("VNote was installed from the Microsoft Store. Updates are delivered "
                       "through the Store.");
    return result;
  }

  if (UpdateInstaller::isUnderProgramFiles(m_installDir)) {
    // MSI installs live there and are managed by Windows Installer.
    result.reason = tr("VNote is installed under Program Files. Please update using the "
                       "installer package.");
    return result;
  }

  if (!UpdateInstaller::isInstallDirWritable(m_installDir)) {
    result.reason = tr("The installation directory is not writable.");
    return result;
  }

  if (!UpdateInstaller::isSameVolume(m_installDir, UpdateInstaller::stagingRoot(m_installDir))) {
    result.reason = tr("The update staging directory is not on the same volume as VNote.");
    return result;
  }

  if (!ManifestSignature::hasTrustedKeys()) {
    // FAIL CLOSED. Without a compiled-in signing key nothing downloaded can be
    // authenticated, and an unsigned update path is strictly worse than no
    // update path. Surfacing it as an eligibility reason (rather than failing
    // later, mid-download) makes an unconfigured build obvious immediately.
    result.reason = tr("This build has no update signing key configured, so updates cannot be "
                       "verified. Please download updates from the releases page.");
    return result;
  }

  if (p_forceProbe || !m_probeCached) {
    m_probeResult = UpdateInstaller::probeAtomicRenameSupport(m_installDir);
    m_probeCached = true;
  }
  if (!m_probeResult) {
    result.reason = tr("This system does not support replacing a running program file safely.");
    return result;
  }

  result.eligible = true;
  return result;
#endif
}

QString UpdateService::variant() const {
  const UpdateManifest local = localManifest();
  if (local.isValid() && !local.variant().isEmpty()) {
    return local.variant();
  }
  return UpdateManifest::variantForBuild();
}

UpdateManifest UpdateService::localManifest() const {
  QFile file(m_installDir + QLatin1Char('/') + UpdateManifest::manifestFileName());
  if (!file.open(QIODevice::ReadOnly)) {
    return UpdateManifest();
  }
  const QByteArray bytes = file.readAll();
  file.close();
  return UpdateManifest::fromJsonBytes(bytes);
}

// ===========================================================================
// Pending lifecycle
// ===========================================================================

bool UpdateService::hasPendingUpdate() const {
  return UpdateInstaller::readPending(m_installDir).isValid();
}

QString UpdateService::pendingVersion() const {
  return UpdateInstaller::readPending(m_installDir).targetVersion;
}

void UpdateService::discardPending() { UpdateInstaller::removeStagingRoot(m_installDir); }

bool UpdateService::revalidatePending() {
  QString error;
  const auto plan = UpdateInstaller::readPending(m_installDir, &error);
  if (!plan.isValid()) {
    if (QFileInfo::exists(UpdateInstaller::pendingPath(m_installDir))) {
      qWarning() << "update: discarding an unusable pending update:" << error;
      discardPending();
    }
    return false;
  }

  auto discard = [&](const QString &p_why) {
    qInfo() << "update: discarding the pending update -" << p_why;
    discardPending();
    return false;
  };

  if (plan.variant != variant()) {
    return discard(QStringLiteral("variant mismatch"));
  }

  const QVersionNumber target = QVersionNumber::fromString(plan.targetVersion);
  const QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
  if (target.isNull() || target <= current) {
    // Already applied, or superseded by a manual install.
    return discard(QStringLiteral("target version is not newer than the installed one"));
  }

  const UpdateManifest manifest = UpdateManifest::fromJson(plan.targetManifest);
  if (!manifest.isValid()) {
    return discard(QStringLiteral("the target manifest no longer parses"));
  }

  for (const QString &relative : plan.staged) {
    UpdateManifestFile expected;
    if (!manifest.lookup(relative, &expected)) {
      return discard(
          QStringLiteral("staged file '%1' is not in the target manifest").arg(relative));
    }
    const QString staged = UpdateInstaller::stagedDir(m_installDir) + QLatin1Char('/') + relative;
    if (!QFileInfo::exists(staged) || QFileInfo(staged).size() != expected.size ||
        hashFileSha256(staged).compare(expected.sha256, Qt::CaseInsensitive) != 0) {
      return discard(QStringLiteral("staged file '%1' no longer verifies").arg(relative));
    }
  }

  return true;
}

UpdateInstaller::StoredResult UpdateService::consumeStoredResult() {
  const auto stored = UpdateInstaller::readResult(m_installDir);
  if (stored.isValid()) {
    UpdateInstaller::clearResult(m_installDir);
    // When nothing else is left in .vnote-update/, drop the directory too.
    QDir staging(UpdateInstaller::stagingRoot(m_installDir));
    if (staging.exists() &&
        staging.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
      staging.removeRecursively();
    }
  }
  return stored;
}

void UpdateService::testSetEndpointOverride(const QUrl &p_apiLatestUrl,
                                            const QUrl &p_assetBaseUrl) {
  m_apiLatestOverride = p_apiLatestUrl;
  m_assetBaseOverride = p_assetBaseUrl;
}

void UpdateService::testSetExtraAllowedHost(const QString &p_host) { m_extraAllowedHost = p_host; }

void UpdateService::testSetPackagedAppOverride(int p_state) { m_packagedAppOverride = p_state; }

// ===========================================================================
// Networking
// ===========================================================================

bool UpdateService::isHostAllowed(const QUrl &p_url) const {
  if (p_url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) {
    // No plain HTTP anywhere, and therefore no HTTPS->HTTP downgrade.
    if (!m_extraAllowedHost.isEmpty() &&
        p_url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0 &&
        p_url.host().compare(m_extraAllowedHost, Qt::CaseInsensitive) == 0) {
      // Local E2E harness only, and only for the explicitly nominated host.
      return true;
    }
    return false;
  }

  const QString host = p_url.host().toLower();
  if (allowedHosts(m_source).contains(host)) {
    return true;
  }
  if (m_source == Source::GitHub && host.endsWith(QLatin1String(".githubusercontent.com"))) {
    // Release assets are redirected here; the exact subdomain has changed over
    // time (objects. -> release-assets.), so the suffix is what is pinned.
    return true;
  }
  if (m_source == Source::Gitee && host.endsWith(QLatin1String(".gitee.com"))) {
    // Gitee hands the actual bytes off to foruda.gitee.com.
    return true;
  }
  if (!m_extraAllowedHost.isEmpty() && host.compare(m_extraAllowedHost, Qt::CaseInsensitive) == 0) {
    return true;
  }
  return false;
}

namespace {

QNetworkRequest buildRequest(const QUrl &p_url, const QString &p_version,
                             UpdateService::Source p_source) {
  QNetworkRequest request(p_url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("VNote/%1 (updater)").arg(p_version));
  if (p_source == UpdateService::Source::Gitee) {
    // Gitee's API rejects unknown vendor media types.
    request.setRawHeader("Accept", "application/json, application/octet-stream, */*");
  } else {
    request.setRawHeader("Accept", "application/vnd.github+json, application/octet-stream, */*");
  }
  // Redirects are followed MANUALLY so every hop can be checked against the
  // allowlist and against an HTTPS->HTTP downgrade. Qt 5 and Qt 6 differ in
  // their DEFAULT policy (Qt 6 follows redirects out of the box), so the policy
  // is set explicitly on EVERY request rather than relied upon.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  return request;
}

// Blocks the CALLING thread on a nested event loop. Only ever used from a
// QtConcurrent worker; never from the GUI thread.
//
// Cancellation is POLLED here rather than only checked between requests:
// a package download waits up to ten minutes, and QNetworkReply::abort() must
// be called on the reply's own thread, which is exactly this one. Without the
// poll, closing VNote during a stalled download would block service teardown
// (and therefore vxcore_context_destroy and any pending update) for the whole
// timeout.
void waitForReply(QNetworkReply *p_reply, int p_timeoutMs,
                  const std::function<bool()> &p_isCancelled, bool *p_timedOut, bool *p_cancelled) {
  QEventLoop loop;

  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
    *p_timedOut = true;
    p_reply->abort();
    loop.quit();
  });

  QTimer cancelPoll;
  cancelPoll.setInterval(c_cancelPollMs);
  QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&]() {
    if (p_isCancelled && p_isCancelled()) {
      *p_cancelled = true;
      p_reply->abort();
      loop.quit();
    }
  });

  QObject::connect(p_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  timeout.start(p_timeoutMs);
  cancelPoll.start();
  loop.exec();
}

} // namespace

bool UpdateService::fetchToMemory(QNetworkAccessManager &p_nam, const QUrl &p_url,
                                  QByteArray *p_out, QString *p_error, qint64 p_maxBytes,
                                  bool *p_notFound) {
  QUrl url = p_url;
  if (p_notFound) {
    *p_notFound = false;
  }

  for (int hop = 0; hop <= c_maxRedirects; ++hop) {
    if (isCancelled()) {
      *p_error = tr("Cancelled.");
      return false;
    }
    if (!isHostAllowed(url)) {
      *p_error = tr("Refusing to contact an unexpected host: %1").arg(url.host());
      return false;
    }

    QNetworkReply *reply = p_nam.get(buildRequest(url, m_currentVersion, m_source));
    bool timedOut = false;
    bool cancelled = false;
    waitForReply(
        reply, c_requestTimeoutMs, [this]() { return isCancelled(); }, &timedOut, &cancelled);

    if (cancelled) {
      reply->deleteLater();
      *p_error = tr("Cancelled.");
      return false;
    }
    if (timedOut) {
      reply->deleteLater();
      *p_error = tr("The request to %1 timed out.").arg(url.host());
      return false;
    }

    const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
      const QUrl next = url.resolved(redirect.toUrl());
      reply->deleteLater();
      if (hop == c_maxRedirects) {
        *p_error = tr("Too many redirects.");
        return false;
      }
      url = next;
      continue;
    }

    if (reply->error() != QNetworkReply::NoError) {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (p_notFound && status == 404) {
        // "Not published here", as opposed to any other transport failure.
        *p_notFound = true;
      }
      *p_error = reply->errorString();
      reply->deleteLater();
      return false;
    }

    const QByteArray data = reply->read(p_maxBytes + 1);
    const bool tooLarge = data.size() > p_maxBytes;
    reply->deleteLater();

    if (tooLarge) {
      *p_error = tr("The response from %1 is unexpectedly large.").arg(url.host());
      return false;
    }

    *p_out = data;
    return true;
  }

  *p_error = tr("Too many redirects.");
  return false;
}

bool UpdateService::downloadToFile(QNetworkAccessManager &p_nam, const QUrl &p_url,
                                   const QString &p_destPath, const QString &p_expectedSha,
                                   qint64 p_expectedSize, QString *p_error) {
  if (p_expectedSize <= 0 || p_expectedSize > c_maxArchiveBytes) {
    *p_error = tr("The download size declared by the manifest is not plausible.");
    return false;
  }

  QDir().mkpath(QFileInfo(p_destPath).absolutePath());

  // A previously downloaded archive that still verifies is reused as-is.
  if (QFileInfo::exists(p_destPath) && QFileInfo(p_destPath).size() == p_expectedSize &&
      hashFileSha256(p_destPath).compare(p_expectedSha, Qt::CaseInsensitive) == 0) {
    return true;
  }
  QFile::remove(p_destPath);

  QUrl url = p_url;
  for (int hop = 0; hop <= c_maxRedirects; ++hop) {
    if (isCancelled()) {
      *p_error = tr("Cancelled.");
      return false;
    }
    if (!isHostAllowed(url)) {
      *p_error = tr("Refusing to download from an unexpected host: %1").arg(url.host());
      return false;
    }

    QNetworkReply *reply = p_nam.get(buildRequest(url, m_currentVersion, m_source));

    QFile file;
    bool overflow = false;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&]() {
      if (!file.isOpen()) {
        return;
      }
      const QByteArray chunk = reply->readAll();
      if (file.size() + chunk.size() > p_expectedSize) {
        overflow = true;
        reply->abort();
        return;
      }
      file.write(chunk);
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [this](qint64 p_done, qint64 p_total) {
                       reportProgress(tr("Downloading"), p_done, p_total);
                     });

    // The redirect target arrives with the headers, so only open the file once
    // we know this hop is the final one.
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [&]() {
      if (reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()) {
        return;
      }
      if (!file.isOpen()) {
        file.setFileName(p_destPath);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
      }
    });

    bool timedOut = false;
    bool cancelled = false;
    waitForReply(
        reply, c_requestTimeoutMs * 10, [this]() { return isCancelled(); }, &timedOut, &cancelled);

    if (file.isOpen()) {
      file.write(reply->readAll());
      file.flush();
      file.close();
    }

    if (cancelled) {
      reply->deleteLater();
      // Leave the partial file: downloadToFile() removes or reuses it on the
      // next attempt after verifying size + hash.
      *p_error = tr("Cancelled.");
      return false;
    }
    if (timedOut) {
      reply->deleteLater();
      QFile::remove(p_destPath);
      *p_error = tr("The download from %1 timed out.").arg(url.host());
      return false;
    }

    const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
      const QUrl next = url.resolved(redirect.toUrl());
      reply->deleteLater();
      if (hop == c_maxRedirects) {
        *p_error = tr("Too many redirects.");
        return false;
      }
      url = next;
      continue;
    }

    const bool failed = reply->error() != QNetworkReply::NoError;
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (overflow) {
      QFile::remove(p_destPath);
      *p_error = tr("The download is larger than the manifest declared.");
      return false;
    }
    if (failed) {
      QFile::remove(p_destPath);
      *p_error = networkError;
      return false;
    }

    if (QFileInfo(p_destPath).size() != p_expectedSize) {
      QFile::remove(p_destPath);
      *p_error = tr("The downloaded file has an unexpected size.");
      return false;
    }
    if (hashFileSha256(p_destPath).compare(p_expectedSha, Qt::CaseInsensitive) != 0) {
      QFile::remove(p_destPath);
      *p_error = tr("The downloaded file failed its integrity check.");
      return false;
    }
    return true;
  }

  *p_error = tr("Too many redirects.");
  return false;
}

QUrl UpdateService::assetUrl(const QString &p_version, const QString &p_assetName) const {
  if (!m_assetBaseOverride.isEmpty()) {
    return m_assetBaseOverride.resolved(QUrl(QStringLiteral("v%1/%2").arg(p_version, p_assetName)));
  }
  const QString &base =
      m_source == Source::Gitee ? c_giteeReleaseDownloadBase : c_githubReleaseDownloadBase;
  return QUrl(QStringLiteral("%1/v%2/%3").arg(base, p_version, p_assetName));
}

QUrl UpdateService::apiLatestUrl() const {
  if (!m_apiLatestOverride.isEmpty()) {
    return m_apiLatestOverride;
  }
  return QUrl(m_source == Source::Gitee ? c_giteeApiLatestUrl : c_githubApiLatestUrl);
}

QString UpdateService::releasePageUrl(const QString &p_tag, const QString &p_htmlUrlFromApi) const {
  if (m_source == Source::GitHub) {
    return p_htmlUrlFromApi;
  }

  // Gitee's release JSON carries no html_url, so the page URL is synthesized.
  // The pattern is <repo>/releases/tag/v<version>, VERIFIED against
  // https://gitee.com/vnotex/vnote/releases/tag/v4.3.0 -- note the `tag/`
  // segment, which the asset download path (/releases/download/v<ver>/...)
  // does NOT have.
  if (!p_tag.isEmpty()) {
    const QUrl synthesized(QStringLiteral("%1/tag/v%2").arg(c_giteeReleasesPageUrl, p_tag));
    if (synthesized.isValid()) {
      return synthesized.toString();
    }
  }
  return c_giteeReleasesPageUrl;
}

QUrl UpdateService::manifestAssetUrl(const QString &p_version) const {
  return assetUrl(p_version, QStringLiteral("VNote-%1-%2.manifest.json").arg(p_version, variant()));
}

QUrl UpdateService::manifestSignatureUrl(const QString &p_version) const {
  // minisign's own convention: <file>.minisig alongside the file.
  return assetUrl(p_version,
                  QStringLiteral("VNote-%1-%2.manifest.json.minisig").arg(p_version, variant()));
}

bool UpdateService::fetchVerifiedManifest(QNetworkAccessManager &p_nam, const QString &p_version,
                                          UpdateManifest *p_out, QString *p_error) {
  return fetchVerifiedManifestEx(p_nam, p_version, p_out, p_error) == ManifestFetch::Ok;
}

UpdateService::ManifestFetch UpdateService::fetchVerifiedManifestEx(QNetworkAccessManager &p_nam,
                                                                    const QString &p_version,
                                                                    UpdateManifest *p_out,
                                                                    QString *p_error) {
  QByteArray manifestBytes;
  bool manifestAbsent = false;
  if (!fetchToMemory(p_nam, manifestAssetUrl(p_version), &manifestBytes, p_error,
                     16LL * 1024 * 1024, &manifestAbsent)) {
    // A 404 on the MANIFEST means this release simply publishes no in-app
    // update package on the selected source. That is a degradation, not an
    // attack, and the caller may offer a check-only experience.
    return manifestAbsent ? ManifestFetch::Absent : ManifestFetch::Error;
  }

  QByteArray signatureBytes;
  if (!fetchToMemory(p_nam, manifestSignatureUrl(p_version), &signatureBytes, p_error, 64 * 1024)) {
    // FAIL CLOSED. Manifest bytes are already in hand, so a missing or
    // unfetchable .minisig (404 included) is a hard refusal, never Absent.
    *p_error = tr("The update manifest for %1 is not signed: %2").arg(p_version, *p_error);
    return ManifestFetch::Error;
  }

  // Verify over the bytes AS RECEIVED. Re-serializing the JSON first would
  // change them and, worse, would mean the thing verified is not the thing
  // parsed.
  QString trustedComment;
  const auto verdict = ManifestSignature::verify(manifestBytes, signatureBytes, &trustedComment);
  if (verdict != ManifestSignature::Result::Valid) {
    *p_error = tr("The update manifest for %1 failed signature verification: %2")
                   .arg(p_version, ManifestSignature::resultToString(verdict));
    qCritical() << "update: REJECTED manifest for" << p_version << "-"
                << ManifestSignature::resultToString(verdict);
    return ManifestFetch::Error;
  }

  QString parseError;
  const UpdateManifest manifest = UpdateManifest::fromJsonBytes(manifestBytes, &parseError);
  if (!manifest.isValid()) {
    *p_error = tr("The update manifest for %1 is malformed: %2").arg(p_version, parseError);
    return ManifestFetch::Error;
  }
  if (manifest.version() != p_version) {
    // A signed manifest for a DIFFERENT version must not be accepted here: that
    // would let an old signed release be replayed in place of a newer one.
    *p_error =
        tr("The manifest published for %1 declares version %2.").arg(p_version, manifest.version());
    return ManifestFetch::Error;
  }

  *p_out = manifest;
  return ManifestFetch::Ok;
}

// ===========================================================================
// Check
// ===========================================================================

void UpdateService::checkForUpdates() {
  bool expected = false;
  if (!m_busy.compare_exchange_strong(expected, true)) {
    return;
  }
  m_cancelled.store(false);

  // Everything below blocks on nested event loops and hashes files, so it runs
  // on a worker. Signals are emitted back on the GUI thread via a queued
  // invocation.
  m_worker = QtConcurrent::run([this]() {
    // QNetworkAccessManager belongs to the thread that creates it, so the
    // manager lives on THIS worker's stack and never escapes it.
    QNetworkAccessManager nam;

    UpdateInfo info;
    info.currentVersion = m_currentVersion;

    const Eligibility eligibility = checkEligibility();
    info.eligible = eligibility.eligible;
    info.ineligibleReason = eligibility.reason;

    QString error;
    QByteArray body;
    const QUrl latestUrl = apiLatestUrl();
    if (!fetchToMemory(nam, latestUrl, &body, &error)) {
      m_busy.store(false);
      reportFailure(error);
      return;
    }

    const QJsonObject release = QJsonDocument::fromJson(body).object();
    QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(QLatin1Char('v'))) {
      tag.remove(0, 1);
    }
    if (tag.isEmpty()) {
      m_busy.store(false);
      reportFailure(tr("The latest release could not be identified."));
      return;
    }

    info.latestVersion = tag;
    info.releaseNotes = release.value(QStringLiteral("body")).toString();
    info.releaseUrl = releasePageUrl(tag, release.value(QStringLiteral("html_url")).toString());

    const QVersionNumber latest = QVersionNumber::fromString(tag);
    const QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
    if (latest.isNull() || latest <= current) {
      m_lastInfo = info;
      m_busy.store(false);
      QMetaObject::invokeMethod(
          this, [this, info]() { emit checkFinished(info); }, Qt::QueuedConnection);
      return;
    }

    info.updateAvailable = true;

    if (!info.eligible) {
      // Nothing else can be planned; the caller opens the releases page.
      m_lastInfo = info;
      m_busy.store(false);
      QMetaObject::invokeMethod(
          this, [this, info]() { emit checkFinished(info); }, Qt::QueuedConnection);
      return;
    }

    UpdateManifest target;
    const ManifestFetch fetched = fetchVerifiedManifestEx(nam, tag, &target, &error);
    if (fetched == ManifestFetch::Absent) {
      // D10: the release exists on this source but publishes no manifest, so
      // there is nothing to plan and nothing to verify. Degrade to check-only
      // and let the caller send the user to the release page. This is NOT a
      // failure: reporting one would look like "could not check for updates".
      qInfo() << "update: no update manifest published for" << tag << "on"
              << sourceToString(m_source) << "- degrading to check-only";
      info.eligible = false;
      info.ineligibleReason =
          tr("This release does not publish in-app update packages on the selected source. "
             "Please download it from the release page.");
      m_lastInfo = info;
      m_busy.store(false);
      QMetaObject::invokeMethod(
          this, [this, info]() { emit checkFinished(info); }, Qt::QueuedConnection);
      return;
    }
    if (fetched != ManifestFetch::Ok) {
      m_busy.store(false);
      reportFailure(error);
      return;
    }
    if (target.variant() != variant()) {
      m_busy.store(false);
      reportFailure(tr("The update manifest is not valid for this build."));
      return;
    }

    const Plan plan = buildPlan(nam, target, &error);
    if (!plan.valid) {
      m_busy.store(false);
      reportFailure(error);
      return;
    }

    m_plan = plan;
    info.isDelta = plan.isDelta;
    info.hopCount = plan.hopVersions.size();
    info.downloadSize = plan.downloadSize;

    m_lastInfo = info;
    m_busy.store(false);
    QMetaObject::invokeMethod(
        this, [this, info]() { emit checkFinished(info); }, Qt::QueuedConnection);
  });
}

// ===========================================================================
// Planning
// ===========================================================================

UpdateService::Plan UpdateService::buildPlan(QNetworkAccessManager &p_nam,
                                             const UpdateManifest &p_target, QString *p_error) {
  Plan plan;
  plan.target = p_target;

  auto fullPackagePlan = [&](const QString &p_reason) {
    if (!p_reason.isEmpty()) {
      qInfo() << "update: falling back to the full package -" << p_reason;
    }
    if (!p_target.fullPackage().isValid()) {
      *p_error = tr("The release does not publish a full package for this build.");
      plan.valid = false;
      return plan;
    }
    plan.isDelta = false;
    plan.hopVersions.clear();
    plan.downloadSize = p_target.fullPackage().size;
    plan.valid = true;
    return plan;
  };

  // --- Delta preconditions -------------------------------------------------
  const UpdateManifest local = localManifest();
  if (!local.isValid()) {
    return fullPackagePlan(QStringLiteral("no local manifest.json"));
  }
  if (!local.isStableChannel()) {
    return fullPackagePlan(QStringLiteral("the installed build is not on the stable channel"));
  }
  if (local.version() != m_currentVersion) {
    return fullPackagePlan(QStringLiteral("the local manifest does not describe this version"));
  }

  // Walk the chain, fetching each intermediate manifest on demand.
  QHash<QString, UpdateManifest> available;
  available.insert(p_target.version(), p_target);

  UpdateManifest cursor = p_target;
  for (int hop = 0; hop < UpdateManifest::c_maxChainHops; ++hop) {
    if (!cursor.hasDelta()) {
      return fullPackagePlan(QStringLiteral("release %1 publishes no delta").arg(cursor.version()));
    }
    const QString baseVersion = cursor.delta().baseVersion;
    if (baseVersion == m_currentVersion || available.contains(baseVersion)) {
      break;
    }

    UpdateManifest intermediate;
    QString fetchError;
    if (!fetchVerifiedManifest(p_nam, baseVersion, &intermediate, &fetchError)) {
      return fullPackagePlan(QStringLiteral("manifest for %1 is unavailable or unverified: %2")
                                 .arg(baseVersion, fetchError));
    }
    available.insert(intermediate.version(), intermediate);
    cursor = intermediate;
  }

  // The PUBLISHED base manifest for the installed version, used to prove the
  // local tree really is that release.
  UpdateManifest publishedBase = available.value(m_currentVersion);
  if (!publishedBase.isValid()) {
    QString fetchError;
    if (!fetchVerifiedManifest(p_nam, m_currentVersion, &publishedBase, &fetchError)) {
      return fullPackagePlan(
          QStringLiteral("the published base manifest is unavailable or unverified: %1")
              .arg(fetchError));
    }
    available.insert(publishedBase.version(), publishedBase);
  }

  QString identityError;
  if (!UpdateManifest::validateBaseIdentity(local, publishedBase, &identityError)) {
    return fullPackagePlan(QStringLiteral("local install is not the published %1: %2")
                               .arg(m_currentVersion, identityError));
  }

  const auto chain = UpdateManifest::resolveChain(p_target, m_currentVersion, available);
  if (!chain.isOk()) {
    return fullPackagePlan(
        QStringLiteral("delta chain rejected (status %1)").arg(static_cast<int>(chain.status)));
  }

  // --- Local integrity precheck -------------------------------------------
  // Any drift from the verified base makes the delta unusable: a patched file
  // would be silently kept.
  const auto &files = publishedBase.files();
  for (int i = 0; i < files.size(); ++i) {
    if (isCancelled()) {
      *p_error = tr("Cancelled.");
      plan.valid = false;
      return plan;
    }
    reportProgress(tr("Verifying the installed files"), i, files.size());

    const QString path = m_installDir + QLatin1Char('/') + files.at(i).path;
    if (!QFileInfo::exists(path) || QFileInfo(path).size() != files.at(i).size ||
        hashFileSha256(path).compare(files.at(i).sha256, Qt::CaseInsensitive) != 0) {
      return fullPackagePlan(QStringLiteral("local file '%1' has drifted from the published base")
                                 .arg(files.at(i).path));
    }
  }

  plan.isDelta = true;
  plan.hopVersions = chain.hopVersions;
  plan.base = publishedBase;
  plan.manifests = available;
  plan.downloadSize = chain.totalDeltaSize;
  plan.valid = true;
  return plan;
}

// ===========================================================================
// Download + stage
// ===========================================================================

UpdateService::DownloadStart UpdateService::startDownload(const QString &p_expectedTargetVersion) {
  // Reserve the worker FIRST. m_plan is written by the check worker, so reading
  // it before winning this compare-exchange would be an unsynchronized read of
  // a value another thread may be assigning -- and would also let a request
  // that arrives mid-check report NoPlan (which emits failed() and hands the
  // caller ownership of a terminal signal) when the truthful answer is Busy.
  //
  // Winning the exchange synchronizes-with the worker's m_busy.store(false),
  // so everything the worker wrote to m_plan is visible below.
  bool expected = false;
  if (!m_busy.compare_exchange_strong(expected, true)) {
    qWarning() << "update: ignoring a download request while another update "
                  "operation is in flight";
    return DownloadStart::Busy;
  }

  if (!m_plan.valid) {
    m_busy.store(false);
    reportFailure(tr("No update has been planned yet."));
    return DownloadStart::NoPlan;
  }

  if (!p_expectedTargetVersion.isEmpty() && m_plan.target.version() != p_expectedTargetVersion) {
    // A newer check replaced the plan under a UI element that still advertises
    // the old one. Downloading the new plan from that button would install a
    // version the user was never shown.
    m_busy.store(false);
    qWarning() << "update: refusing a download for" << p_expectedTargetVersion
               << "- the current plan targets" << m_plan.target.version();
    return DownloadStart::Stale;
  }

  m_cancelled.store(false);

  m_worker = QtConcurrent::run([this]() {
    QString error;
    QNetworkAccessManager nam;
    const bool ok = stagePlan(nam, m_plan, &error);
    m_busy.store(false);

    if (!ok) {
      reportFailure(error);
      return;
    }

    const QString version = m_plan.target.version();
    QMetaObject::invokeMethod(
        this, [this, version]() { emit readyToApply(version); }, Qt::QueuedConnection);
  });

  return DownloadStart::Started;
}

bool UpdateService::hasEnoughFreeSpace(const Plan &p_plan, QString *p_error) const {
  qint64 archives = p_plan.downloadSize;
  // Expanded sizes come from the TARGET MANIFEST, never from a compressed size.
  qint64 expanded = 0;
  if (p_plan.isDelta) {
    for (const QString &path : UpdateManifest::expectedChanged(p_plan.base, p_plan.target)) {
      UpdateManifestFile entry;
      if (p_plan.target.lookup(path, &entry)) {
        expanded += entry.size;
      }
    }
  } else {
    expanded = p_plan.target.totalExpandedSize();
  }

  const qint64 required = archives + expanded + c_freeSpaceHeadroom;
  const QStorageInfo storage(m_installDir);
  if (!storage.isValid()) {
    // Cannot tell; do not block the update on a missing measurement.
    return true;
  }
  if (storage.bytesAvailable() < required) {
    *p_error = tr("Not enough free disk space: %1 MB are required.").arg(required / (1024 * 1024));
    return false;
  }
  return true;
}

bool UpdateService::stagePlan(QNetworkAccessManager &p_nam, const Plan &p_plan, QString *p_error) {
  if (!hasEnoughFreeSpace(p_plan, p_error)) {
    return false;
  }

  const QString downloadDir = UpdateInstaller::downloadDir(m_installDir);
  const QString stagedDir = UpdateInstaller::stagedDir(m_installDir);

  // Start from a clean staging tree AND drop any previous plan: a leftover from
  // an abandoned plan would otherwise survive the completeness check as an
  // unexpected extra file, or leave a stale pending.json pointing at files that
  // are about to be replaced.
  UpdateInstaller::clearPending(m_installDir);
  QDir(stagedDir).removeRecursively();
  if (!QDir().mkpath(stagedDir) || !QDir().mkpath(downloadDir)) {
    *p_error = tr("Could not create the update staging directory.");
    return false;
  }

  QStringList expectedChanged;
  QByteArray manifestFromArchive;

  if (p_plan.isDelta) {
    expectedChanged = UpdateManifest::expectedChanged(p_plan.base, p_plan.target);

    // Oldest hop first, so newer blobs overwrite older ones.
    for (const QString &version : p_plan.hopVersions) {
      if (isCancelled()) {
        *p_error = tr("Cancelled.");
        return false;
      }

      const UpdateManifest hopTarget = p_plan.manifests.value(version);
      const QString hopBaseVersion = hopTarget.delta().baseVersion;
      const UpdateManifest hopBase = hopBaseVersion == p_plan.base.version()
                                         ? p_plan.base
                                         : p_plan.manifests.value(hopBaseVersion);
      if (!hopTarget.isValid() || !hopBase.isValid()) {
        *p_error = tr("The update chain is incomplete.");
        return false;
      }

      const QString asset = hopTarget.delta().asset;
      const QString archive = downloadDir + QLatin1Char('/') + asset;
      reportProgress(tr("Downloading %1").arg(version), 0, hopTarget.delta().size);
      if (!downloadToFile(p_nam, assetUrl(version, asset), archive, hopTarget.delta().sha256,
                          hopTarget.delta().size, p_error)) {
        return false;
      }

      // Per-hop entry-set equality: the archive must contain EXACTLY the files
      // this hop changes, no more and no less.
      ZipExtractor::Options options;
      options.stripTopLevelDir = false; // delta archives are install-root relative
      for (const QString &path : UpdateManifest::hopArchiveSet(hopBase, hopTarget)) {
        UpdateManifestFile entry;
        hopTarget.lookup(path, &entry);
        options.expectedEntries.insert(UpdateManifest::pathKey(path), entry.size);
      }

      reportProgress(tr("Extracting %1").arg(version), 0, 0);
      const auto extraction = ZipExtractor::extract(archive, stagedDir, options);
      if (!extraction.isOk()) {
        *p_error =
            tr("The update package for %1 was rejected: %2").arg(version, extraction.message);
        return false;
      }
    }
  } else {
    const auto &full = p_plan.target.fullPackage();
    const QString archive = downloadDir + QLatin1Char('/') + full.asset;
    reportProgress(tr("Downloading"), 0, full.size);
    if (!downloadToFile(p_nam, assetUrl(p_plan.target.version(), full.asset), archive, full.sha256,
                        full.size, p_error)) {
      return false;
    }

    ZipExtractor::Options options;
    // CPack wraps the full ZIP in a single "VNote-<ver>-<variant>/" directory.
    options.stripTopLevelDir = true;
    options.maxTotalUncompressedSize =
        p_plan.target.totalExpandedSize() + 16LL * 1024 * 1024; // manifest.json + slack

    reportProgress(tr("Extracting"), 0, 0);
    const auto extraction = ZipExtractor::extract(archive, stagedDir, options);
    if (!extraction.isOk()) {
      *p_error = tr("The update package was rejected: %1").arg(extraction.message);
      return false;
    }

    // The full package stages EVERY file.
    expectedChanged = QStringList();
    for (const auto &entry : p_plan.target.files()) {
      expectedChanged.append(entry.path);
    }
    expectedChanged.sort();
  }

  // manifest.json is handled OUT OF BAND: it is extracted with everything else
  // but must never be moved by the swap, and its contents are compared against
  // the release manifest.
  //
  // On the FULL-PACKAGE path it is mandatory and must match the published
  // manifest exactly, including the whole files[] map -- that package is what
  // becomes the next delta base, and a manifest that disagrees with the bytes
  // on disk would silently poison every later incremental update. Delta
  // archives legitimately omit it (they only carry changed files).
  const QString stagedManifest = stagedDir + QLatin1Char('/') + UpdateManifest::manifestFileName();
  const bool haveStagedManifest = QFileInfo::exists(stagedManifest);

  if (!haveStagedManifest && !p_plan.isDelta) {
    *p_error = tr("The update package does not contain a manifest.");
    return false;
  }

  if (haveStagedManifest) {
    QFile file(stagedManifest);
    if (file.open(QIODevice::ReadOnly)) {
      manifestFromArchive = file.readAll();
      file.close();
    }
    QFile::remove(stagedManifest);

    QString manifestError;
    const UpdateManifest inPackage =
        UpdateManifest::fromJsonBytes(manifestFromArchive, &manifestError);

    if (!inPackage.isValid()) {
      if (!p_plan.isDelta) {
        *p_error = tr("The package manifest is not valid: %1").arg(manifestError);
        return false;
      }
      qWarning() << "update: ignoring an unparsable manifest.json inside a delta archive:"
                 << manifestError;
    } else {
      if (inPackage.version() != p_plan.target.version() ||
          inPackage.variant() != p_plan.target.variant() ||
          inPackage.platform() != p_plan.target.platform() ||
          inPackage.commit() != p_plan.target.commit() ||
          inPackage.channel() != p_plan.target.channel() ||
          inPackage.product() != p_plan.target.product()) {
        *p_error = tr("The package manifest does not match the published manifest.");
        return false;
      }

      if (!p_plan.isDelta) {
        // Full package: the complete file map must agree too.
        if (inPackage.fileMap().size() != p_plan.target.fileMap().size()) {
          *p_error = tr("The package manifest lists a different set of files than the "
                        "published manifest.");
          return false;
        }
        for (auto it = p_plan.target.fileMap().constBegin();
             it != p_plan.target.fileMap().constEnd(); ++it) {
          const auto packaged = inPackage.fileMap().constFind(it.key());
          if (packaged == inPackage.fileMap().constEnd() || packaged.value() != it.value()) {
            *p_error = tr("The package manifest disagrees with the published manifest about "
                          "'%1'.")
                           .arg(it.value().path);
            return false;
          }
        }
      }
    }
  }

  if (!verifyStagedTree(p_plan, expectedChanged, p_plan.isDelta, p_error)) {
    return false;
  }

  // --- Deletions ----------------------------------------------------------
  QStringList deletions;
  const UpdateManifest local = localManifest();
  if (local.isValid()) {
    deletions = UpdateManifest::deletions(local, p_plan.target);
  }
  // Documented consequence: with no local manifest there is nothing to diff
  // against, so obsolete files from a pre-manifest install linger until the
  // next managed update.

  UpdateInstaller::PendingPlan pending;
  pending.targetVersion = p_plan.target.version();
  pending.variant = p_plan.target.variant();
  pending.executablePath = QFileInfo(UpdateInstaller::exePathFromModulePath()).fileName();
  pending.staged = expectedChanged;
  pending.deletions = deletions;
  pending.targetManifest = p_plan.target.toJson();

  if (!UpdateInstaller::writePending(m_installDir, pending)) {
    *p_error = tr("Could not record the pending update.");
    return false;
  }

  // The archives are no longer needed once everything is staged and verified.
  QDir(downloadDir).removeRecursively();
  return true;
}

bool UpdateService::verifyStagedTree(const Plan &p_plan, const QStringList &p_expectedChanged,
                                     bool p_allowPrune, QString *p_error) {
  const QString stagedDir = UpdateInstaller::stagedDir(m_installDir);
  const QDir root(stagedDir);

  // Everything actually present under staged/.
  QStringList stagedPaths;
  QDirIterator it(stagedDir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    stagedPaths.append(root.relativeFilePath(it.next()));
  }

  QSet<QString> expectedKeys;
  for (const QString &path : p_expectedChanged) {
    expectedKeys.insert(UpdateManifest::pathKey(path));
  }

  for (const QString &path : stagedPaths) {
    if (expectedKeys.contains(UpdateManifest::pathKey(path))) {
      continue;
    }

    if (!p_allowPrune) {
      // Full-package path: the archive's entry set must equal the target's
      // exactly, so an extra file means the package is not what it claims.
      *p_error = tr("The update package contains an unexpected file: %1").arg(path);
      return false;
    }

    // Delta path: PRUNE every staged path outside the expectation. This covers
    // BOTH intermediate-only paths (added by a hop, removed by a later one) and
    // paths changed by a hop and then reverted to the base hash by the target.
    QFile::remove(stagedDir + QLatin1Char('/') + path);
  }

  // Now require exact equality, and verify size + SHA-256 for every entry.
  for (int i = 0; i < p_expectedChanged.size(); ++i) {
    if (isCancelled()) {
      *p_error = tr("Cancelled.");
      return false;
    }
    reportProgress(tr("Verifying the download"), i, p_expectedChanged.size());

    const QString relative = p_expectedChanged.at(i);
    const QString staged = stagedDir + QLatin1Char('/') + relative;

    UpdateManifestFile expected;
    if (!p_plan.target.lookup(relative, &expected)) {
      *p_error = tr("Internal error: '%1' is not in the target manifest.").arg(relative);
      return false;
    }
    if (!QFileInfo::exists(staged)) {
      *p_error = tr("The update package is missing '%1'.").arg(relative);
      return false;
    }
    if (QFileInfo(staged).size() != expected.size ||
        hashFileSha256(staged).compare(expected.sha256, Qt::CaseInsensitive) != 0) {
      *p_error = tr("The staged file '%1' failed its integrity check.").arg(relative);
      return false;
    }
  }

  return true;
}

// ===========================================================================
// Misc
// ===========================================================================

void UpdateService::cancel() { m_cancelled.store(true); }

void UpdateService::reportProgress(const QString &p_stage, qint64 p_done, qint64 p_total) {
  QMetaObject::invokeMethod(
      this, [this, p_stage, p_done, p_total]() { emit progress(p_stage, p_done, p_total); },
      Qt::QueuedConnection);
}

void UpdateService::reportFailure(const QString &p_message) {
  QMetaObject::invokeMethod(
      this, [this, p_message]() { emit failed(p_message); }, Qt::QueuedConnection);
}
