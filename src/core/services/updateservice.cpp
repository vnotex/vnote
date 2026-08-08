#include "updateservice.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
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
constexpr qint64 UpdateService::c_maxApiResponseBytes;

namespace {

const QString c_githubApiLatestUrl =
    QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest");

const QString c_githubReleasesPageUrl = QStringLiteral("https://github.com/vnotex/vnote/releases");

const QString c_giteeApiLatestUrl =
    QStringLiteral("https://gitee.com/api/v5/repos/vnotex/vnote/releases/latest");

const QString c_giteeReleasesPageUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases");

// Overall request timeout. QNetworkAccessManager's own transfer timeout only
// covers stalls, not a slow-but-alive server.
constexpr int c_requestTimeoutMs = 60 * 1000;

// How often a blocking request re-checks the cancellation flag. Bounds how long
// service teardown can wait on an in-flight request.
constexpr int c_cancelPollMs = 250;

} // namespace

const QStringList &UpdateService::allowedHosts(Source p_source) {
  // The API host is the only one strictly needed, but the release pages and
  // asset hosts are kept so a redirect within the same forge is still followed.
  static const QStringList githubHosts{QStringLiteral("api.github.com"),
                                       QStringLiteral("github.com"),
                                       QStringLiteral("codeload.github.com")};
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

void UpdateService::testSetEndpointOverride(const QUrl &p_apiLatestUrl) {
  m_apiLatestOverride = p_apiLatestUrl;
}

void UpdateService::testSetExtraAllowedHost(const QString &p_host) { m_extraAllowedHost = p_host; }

// ===========================================================================
// Networking
// ===========================================================================

bool UpdateService::isHostAllowed(const QUrl &p_url) const {
  if (p_url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) {
    // No plain HTTP anywhere, and therefore no HTTPS->HTTP downgrade.
    if (!m_extraAllowedHost.isEmpty() &&
        p_url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0 &&
        p_url.host().compare(m_extraAllowedHost, Qt::CaseInsensitive) == 0) {
      // Local test harness only, and only for the explicitly nominated host.
      return true;
    }
    return false;
  }

  const QString host = p_url.host().toLower();
  if (allowedHosts(m_source).contains(host)) {
    return true;
  }
  if (m_source == Source::GitHub && host.endsWith(QLatin1String(".githubusercontent.com"))) {
    // The API can redirect here; the exact subdomain has changed over time
    // (objects. -> release-assets.), so the suffix is what is pinned.
    return true;
  }
  if (m_source == Source::Gitee && host.endsWith(QLatin1String(".gitee.com"))) {
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
                    QStringLiteral("VNote/%1 (update check)").arg(p_version));
  if (p_source == UpdateService::Source::Gitee) {
    // Gitee's API rejects unknown vendor media types.
    request.setRawHeader("Accept", "application/json, */*");
  } else {
    request.setRawHeader("Accept", "application/vnd.github+json, */*");
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

bool UpdateService::fetchToMemory(QNetworkAccessManager &p_nam, const QUrl &p_url,
                                  QByteArray *p_out, QString *p_error, qint64 p_maxBytes) {
  QUrl url = p_url;

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

    // The cap is enforced WHILE the body arrives: a hostile or broken server
    // must not be able to make VNote buffer an unbounded response and only be
    // rejected afterwards.
    QByteArray body;
    bool tooLarge = false;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&]() {
      body.append(reply->readAll());
      if (body.size() > p_maxBytes) {
        tooLarge = true;
        reply->abort();
      }
    });

    bool timedOut = false;
    bool cancelled = false;
    waitForReply(
        reply, c_requestTimeoutMs, [this]() { return isCancelled(); }, &timedOut, &cancelled);

    if (tooLarge) {
      reply->deleteLater();
      *p_error = tr("The response from %1 is unexpectedly large.").arg(url.host());
      return false;
    }
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
      *p_error = reply->errorString();
      reply->deleteLater();
      return false;
    }

    body.append(reply->readAll());
    reply->deleteLater();

    if (body.size() > p_maxBytes) {
      *p_error = tr("The response from %1 is unexpectedly large.").arg(url.host());
      return false;
    }

    *p_out = body;
    return true;
  }

  *p_error = tr("Too many redirects.");
  return false;
}

QUrl UpdateService::apiLatestUrl() const {
  if (!m_apiLatestOverride.isEmpty()) {
    return m_apiLatestOverride;
  }
  return QUrl(m_source == Source::Gitee ? c_giteeApiLatestUrl : c_githubApiLatestUrl);
}

QString UpdateService::releasePageUrl(const QString &p_tag, const QString &p_htmlUrlFromApi) const {
  if (m_source == Source::GitHub) {
    // The API's own page URL, but only when it is actually usable: it is handed
    // straight to QDesktopServices, and UpdateDialog has no "empty URL" branch,
    // so an absent or off-forge value would render a dead button (or open
    // something the user did not ask for). isHostAllowed() applies the same
    // https-only, source-scoped rule a redirect hop gets.
    const QUrl fromApi(p_htmlUrlFromApi);
    if (fromApi.isValid() && !fromApi.host().isEmpty() && isHostAllowed(fromApi)) {
      return fromApi.toString();
    }
    if (!p_htmlUrlFromApi.isEmpty()) {
      qWarning() << "update: ignoring an unusable release page URL from the API:"
                 << p_htmlUrlFromApi;
    }
    if (!p_tag.isEmpty()) {
      const QUrl synthesized(QStringLiteral("%1/tag/v%2").arg(c_githubReleasesPageUrl, p_tag));
      if (synthesized.isValid()) {
        return synthesized.toString();
      }
    }
    return c_githubReleasesPageUrl;
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

  // fetchToMemory blocks on a nested event loop, so the check runs on a worker.
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
    QByteArray body;
    if (!fetchToMemory(nam, apiLatestUrl(), &body, &error)) {
      fail(error);
      return;
    }

    const QJsonObject release = QJsonDocument::fromJson(body).object();
    QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(QLatin1Char('v'))) {
      tag.remove(0, 1);
    }
    if (tag.isEmpty()) {
      fail(tr("The latest release could not be identified."));
      return;
    }

    info.latestVersion = tag;
    info.releaseNotes = release.value(QStringLiteral("body")).toString();
    info.releaseUrl = releasePageUrl(tag, release.value(QStringLiteral("html_url")).toString());

    const QVersionNumber latest = QVersionNumber::fromString(tag);
    const QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
    info.updateAvailable = !latest.isNull() && latest > current;

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
