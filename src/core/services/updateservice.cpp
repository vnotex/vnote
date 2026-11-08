#include "updateservice.h"

#include <QEventLoop>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVersionNumber>
#include <QtConcurrent>

#include <functional>

using namespace vnotex;

constexpr int UpdateService::c_maxRedirects;

namespace {

const QString c_githubReleasesPageUrl = QStringLiteral("https://github.com/vnotex/vnote/releases");

const QString c_giteeReleasesPageUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases");

// Overall request timeout. QNetworkAccessManager's own transfer timeout only
// covers stalls, not a slow-but-alive server.
constexpr int c_requestTimeoutMs = 60 * 1000;

// How often a blocking request re-checks the cancellation flag. Bounds how long
// service teardown can wait on an in-flight request.
constexpr int c_cancelPollMs = 250;

} // namespace

const QStringList &UpdateService::allowedHosts(Source p_source) {
  static const QStringList githubHosts{QStringLiteral("github.com")};
  static const QStringList giteeHosts{QStringLiteral("gitee.com")};

  return p_source == Source::Gitee ? giteeHosts : githubHosts;
}

UpdateService::Source UpdateService::sourceFromString(const QString &p_source) {
  // Gitee is the default: only an EXPLICIT "github" selects GitHub. Empty,
  // unknown and absent values all mean Gitee.
  return p_source.trimmed().compare(QLatin1String("github"), Qt::CaseInsensitive) == 0
             ? Source::GitHub
             : Source::Gitee;
}

QString UpdateService::sourceToString(Source p_source) {
  return p_source == Source::Gitee ? QStringLiteral("gitee") : QStringLiteral("github");
}

void UpdateService::setSource(Source p_source) {
  if (m_source == p_source) {
    return;
  }
  if (m_busy.load()) {
    // Switching origins mid-flight would let one check mix a request to one
    // forge with a response parsed as the other's.
    qWarning() << "update: ignoring a source change to" << sourceToString(p_source)
               << "while a check is in flight";
    return;
  }
  m_source = p_source;
}

QUrl UpdateService::releasesPageUrl() const {
  return QUrl(m_source == Source::Gitee ? c_giteeReleasesPageUrl : c_githubReleasesPageUrl);
}

UpdateService::UpdateService(const QString &p_currentVersion, QObject *p_parent)
    : QObject(p_parent), m_currentVersion(p_currentVersion) {
  qRegisterMetaType<vnotex::UpdateInfo>("vnotex::UpdateInfo");
}

UpdateService::~UpdateService() {
  cancel();
  // Every worker holds a raw `this`; none may outlive the object.
  waitForWorkers();
}

void UpdateService::testSetEndpointOverride(const QUrl &p_latestReleaseUrl) {
  m_latestReleaseOverride = p_latestReleaseUrl;
}

void UpdateService::testSetExtraAllowedHost(const QString &p_host) { m_extraAllowedHost = p_host; }

// ===========================================================================
// Networking
// ===========================================================================

bool UpdateService::isHostAllowed(const QUrl &p_url) const {
  if (!p_url.isValid() || p_url.hasQuery() || p_url.hasFragment() ||
      p_url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))) {
    return false;
  }
  if (!m_latestReleaseOverride.isEmpty()) {
    // A fixture may contact only its explicitly nominated origin, not production.
    return !m_extraAllowedHost.isEmpty() &&
           p_url.host().compare(m_extraAllowedHost, Qt::CaseInsensitive) == 0 &&
           p_url.scheme() == m_latestReleaseOverride.scheme() &&
           p_url.port() == m_latestReleaseOverride.port();
  }
  return p_url.scheme() == QLatin1String("https") && p_url.port(443) == 443 &&
         allowedHosts(m_source).contains(p_url.host(), Qt::CaseInsensitive);
}

namespace {

QNetworkRequest buildRequest(const QUrl &p_url, const QString &p_version) {
  QNetworkRequest request(p_url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("VNote/%1 (update check)").arg(p_version));
  // Redirects are followed MANUALLY so every hop can be checked against the
  // allowlist and against an HTTPS->HTTP downgrade. Override Qt 6's default
  // redirect following explicitly on EVERY request.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  return request;
}

// Blocks the CALLING thread on a nested event loop. Only ever used from a
// QtConcurrent worker; never from the GUI thread.
//
// Cancellation is POLLED here rather than only checked between requests:
// QNetworkReply::abort() must be called on the reply's own thread, which is
// exactly this one. Without the poll, closing VNote during a stalled request
// would block service teardown (and therefore vxcore_context_destroy) for the
// whole timeout.
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

bool UpdateService::findLatestRelease(QNetworkAccessManager &p_nam, UpdateInfo *p_info,
                                      QString *p_error) {
  QUrl url = latestReleaseUrl();
  const QString latestPath = QStringLiteral("/vnotex/vnote/releases/latest");
  const QString tagPrefix = QStringLiteral("/vnotex/vnote/releases/tag/v");

  for (int hop = 0; hop <= c_maxRedirects; ++hop) {
    if (isCancelled()) {
      *p_error = tr("Cancelled.");
      return false;
    }
    if (!isHostAllowed(url)) {
      *p_error = tr("Refusing to contact an unexpected host: %1").arg(url.host());
      return false;
    }
    const QString path = url.path(QUrl::FullyEncoded);
    if (path != latestPath && path != QStringLiteral("/vnotex/vnote/releases/latest/")) {
      *p_error = tr("The latest release could not be identified.");
      return false;
    }

    QNetworkReply *reply = p_nam.head(buildRequest(url, m_currentVersion));
    bool timedOut = false;
    bool cancelled = false;
    waitForReply(
        reply, c_requestTimeoutMs, [this]() { return isCancelled(); }, &timedOut, &cancelled);
    if (cancelled || isCancelled()) {
      reply->deleteLater();
      *p_error = tr("Cancelled.");
      return false;
    }
    if (timedOut) {
      reply->deleteLater();
      *p_error = tr("The request to %1 timed out.").arg(url.host());
      return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
      *p_error = reply->errorString();
      reply->deleteLater();
      return false;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    reply->deleteLater();
    if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
        !location.isValid() || location.isEmpty()) {
      *p_error = tr("The latest release could not be identified.");
      return false;
    }
    if (hop == c_maxRedirects) {
      *p_error = tr("Too many redirects.");
      return false;
    }
    const QUrl next = url.resolved(location);
    if (!isHostAllowed(next)) {
      *p_error = tr("Refusing to contact an unexpected host: %1").arg(next.host());
      return false;
    }
    const QString nextPath = next.path(QUrl::FullyEncoded);
    if (nextPath.startsWith(tagPrefix)) {
      const QString version = nextPath.mid(tagPrefix.size());
      qsizetype suffix = 0;
      const QVersionNumber latest = QVersionNumber::fromString(version, &suffix);
      if (latest.segmentCount() != 3 || suffix != version.size()) {
        *p_error = tr("The latest release could not be identified.");
        return false;
      }
      p_info->latestVersion = version;
      p_info->updateAvailable = latest > QVersionNumber::fromString(m_currentVersion);
      QUrl page = releasesPageUrl();
      if (m_source == Source::Gitee) {
        // Gitee's tag URL redirects to a source archive, not release notes.
        page.setFragment(QStringLiteral("release-v%1").arg(version));
      } else {
        page.setPath(nextPath);
      }
      p_info->releaseUrl = page.toString();
      // Do not request the tag URL or a response body. Notes stay on the page.
      return true;
    }
    url = next;
  }
  *p_error = tr("Too many redirects.");
  return false;
}

QUrl UpdateService::latestReleaseUrl() const {
  if (!m_latestReleaseOverride.isEmpty()) {
    return m_latestReleaseOverride;
  }
  return QUrl(releasesPageUrl().toString() + QStringLiteral("/latest"));
}

// ===========================================================================
// Check
// ===========================================================================

bool UpdateService::checkForUpdates() {
  bool expected = false;
  if (!m_busy.compare_exchange_strong(expected, true)) {
    // Dropped, not queued: two workers would both write a terminal signal and
    // the caller could not tell which result belonged to which request.
    return false;
  }
  m_cancelled.store(false);

  // HEAD discovery blocks on a nested event loop, so the check runs on a worker.
  // Signals are emitted back on the GUI thread via a queued invocation.
  trackWorker(QtConcurrent::run([this]() {
    // QNetworkAccessManager belongs to the thread that creates it, so the
    // manager lives on THIS worker's stack and never escapes it.
    QNetworkAccessManager nam;

    UpdateInfo info;
    info.currentVersion = m_currentVersion;

    // m_busy is released LAST on every path, after the terminal signal has been
    // queued, so the next check cannot start while this one is still running.
    auto finish = [this](const UpdateInfo &p_info) {
      QMetaObject::invokeMethod(
          this, [this, p_info]() { emit checkFinished(p_info); }, Qt::QueuedConnection);
      m_busy.store(false);
    };
    auto fail = [this](const QString &p_error) {
      reportFailure(p_error);
      m_busy.store(false);
    };

    QString error;
    if (!findLatestRelease(nam, &info, &error)) {
      fail(error);
      return;
    }

    finish(info);
  }));

  return true;
}

void UpdateService::trackWorker(const QFuture<void> &p_worker) {
  QMutexLocker locker(&m_workerMutex);
  // Drop the ones that have already finished so the list cannot grow without
  // bound over a long session.
  for (int i = m_workers.size() - 1; i >= 0; --i) {
    if (m_workers.at(i).isFinished()) {
      m_workers.removeAt(i);
    }
  }
  m_workers.append(p_worker);
}

void UpdateService::waitForWorkers() {
  // Snapshot under the lock and wait OUTSIDE it: waiting with the mutex held
  // would deadlock against trackWorker(). Every worker holds a raw `this`, so
  // none may outlive the object.
  QVector<QFuture<void>> pending;
  {
    QMutexLocker locker(&m_workerMutex);
    pending = m_workers;
  }
  for (QFuture<void> &worker : pending) {
    worker.waitForFinished();
  }
}

// ===========================================================================
// Misc
// ===========================================================================

void UpdateService::cancel() { m_cancelled.store(true); }

void UpdateService::reportFailure(const QString &p_message) {
  QMetaObject::invokeMethod(
      this, [this, p_message]() { emit failed(p_message); }, Qt::QueuedConnection);
}
