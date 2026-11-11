#include "updateservice.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTextDocument>
#include <QTimer>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QtConcurrent>

#include <functional>
#include <utility>

using namespace vnotex;

constexpr int UpdateService::c_maxRedirects;

namespace {

const QString c_githubReleasesPageUrl = QStringLiteral("https://github.com/vnotex/vnote/releases");

const QString c_giteeReleasesPageUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases");

// Overall request timeout. QNetworkAccessManager's own transfer timeout only
// covers stalls, not a slow-but-alive server.
constexpr int c_requestTimeoutMs = 60 * 1000;

// Notes are optional: bound both the extra wait and in-memory metadata.
constexpr int c_notesTimeoutMs = 10 * 1000;
constexpr qint64 c_maxNotesBytes = 2 * 1024 * 1024;

// How often a blocking request re-checks the cancellation flag. Bounds how long
// service teardown can wait on an in-flight request.
constexpr int c_cancelPollMs = 250;

// HTML is metadata, never a request to load images, stylesheets or local files.
class ReleaseNotesDocument : public QTextDocument {
protected:
  QVariant loadResource(int, const QUrl &) override { return QByteArray(); }
};

QString githubReleaseNotes(const QByteArray &p_feed, const QString &p_releaseUrl) {
  QXmlStreamReader xml(p_feed);
  const auto atom = QLatin1String("http://www.w3.org/2005/Atom");
  if (!xml.readNextStartElement() || xml.name() != QLatin1String("feed") ||
      xml.namespaceUri() != atom) {
    return {};
  }

  QString content;
  QString type;
  int matches = 0;
  while (xml.readNextStartElement()) {
    if (xml.name() != QLatin1String("entry") || xml.namespaceUri() != atom) {
      xml.skipCurrentElement();
      continue;
    }
    bool matchesRelease = false;
    QString entryContent;
    QString entryType;
    while (xml.readNextStartElement()) {
      if (xml.namespaceUri() == atom && xml.name() == QLatin1String("link")) {
        const auto attributes = xml.attributes();
        if (attributes.value(QLatin1String("rel")) == QLatin1String("alternate") &&
            attributes.value(QLatin1String("href")) == p_releaseUrl) {
          matchesRelease = true;
        }
        xml.skipCurrentElement();
      } else if (xml.namespaceUri() == atom && xml.name() == QLatin1String("content") &&
                 !xml.attributes().hasAttribute(QLatin1String("src"))) {
        entryType = xml.attributes().value(QLatin1String("type")).toString();
        entryContent = xml.readElementText();
      } else {
        xml.skipCurrentElement();
      }
    }
    if (matchesRelease) {
      ++matches;
      content = std::move(entryContent);
      type = std::move(entryType);
    }
  }
  // Reject truncated/ambiguous feeds rather than showing another version's notes.
  while (!xml.atEnd()) {
    xml.readNext();
  }
  if (xml.hasError() || matches != 1 || content.trimmed().isEmpty()) {
    return {};
  }
  ReleaseNotesDocument document;
  if (type == QLatin1String("html")) {
    document.setHtml(content);
  } else if (type.isEmpty() || type == QLatin1String("text")) {
    document.setPlainText(content);
  } else {
    return {};
  }
  return document.toMarkdown();
}

QString giteeReleaseNotes(const QByteArray &p_page, const QString &p_version) {
  const QString page = QString::fromUtf8(p_page);
  // The sidebar repeats data-tag too. Only inspect the release's content container,
  // and stop at the next release so a missing textarea cannot borrow older notes.
  static const QRegularExpression release(
      QStringLiteral(
          R"(<div\b[^>]*\bclass\s*=\s*(['"])(?:[^'"]*\s)?release-tag-item(?:\s[^'"]*)?\1[^>]*>)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression tag(QStringLiteral(R"(\bdata-tag\s*=\s*(['"])([^'"]*)\1)"));
  static const QRegularExpression textarea(
      QStringLiteral(
          R"(<textarea\b[^>]*\bclass\s*=\s*(['"])(?:[^'"]*\s)?content(?:\s[^'"]*)?\1[^>]*>([\s\S]*?)</textarea\s*>)"),
      QRegularExpression::CaseInsensitiveOption);
  const QString targetTag = QLatin1Char('v') + p_version;
  auto releases = release.globalMatch(page);
  while (releases.hasNext()) {
    const auto item = releases.next();
    const auto itemTag = tag.matchView(item.capturedView());
    if (itemTag.capturedView(2) != targetTag) {
      continue;
    }
    const qsizetype end = releases.hasNext() ? releases.peekNext().capturedStart() : page.size();
    const auto notes =
        textarea.matchView(QStringView(page).sliced(item.capturedEnd(), end - item.capturedEnd()));
    if (!notes.hasMatch()) {
      return {};
    }
    // Textarea contents are raw text with character references, not HTML. Escape
    // raw angle brackets before decoding once; &lt;b&gt; must also stay literal.
    QString escaped = notes.captured(2);
    escaped.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    escaped.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    ReleaseNotesDocument document;
    document.setHtml(QStringLiteral("<pre>%1</pre>").arg(escaped));
    const QString markdown = document.toPlainText();
    return markdown.trimmed().isEmpty() ? QString() : markdown;
  }
  return {};
}

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
      // Discovery stops here; notes use a separate metadata endpoint, never this tag URL.
      return true;
    }
    url = next;
  }
  *p_error = tr("Too many redirects.");
  return false;
}

QString UpdateService::fetchReleaseNotes(QNetworkAccessManager &p_nam, const UpdateInfo &p_info) {
  const QString path = m_source == Source::GitHub ? QStringLiteral("/vnotex/vnote/releases.atom")
                                                  : QStringLiteral("/vnotex/vnote/releases");
  QUrl url = latestReleaseUrl();
  url.setPath(path);
  QElapsedTimer deadline;
  deadline.start();

  for (int hop = 0; hop <= c_maxRedirects; ++hop) {
    const int remaining = c_notesTimeoutMs - static_cast<int>(deadline.elapsed());
    if (remaining <= 0 || isCancelled() || !isHostAllowed(url) ||
        (url.path(QUrl::FullyEncoded) != path &&
         url.path(QUrl::FullyEncoded) != path + QLatin1Char('/'))) {
      return {};
    }

    QScopedPointer<QNetworkReply> reply(p_nam.get(buildRequest(url, m_currentVersion)));
    reply->setReadBufferSize(64 * 1024);
    QByteArray body;
    bool tooLarge = false;
    const auto readBody = [&]() {
      // Read at most one byte past the cap, even with no/incorrect Content-Length.
      body.append(reply->read(c_maxNotesBytes + 1 - body.size()));
      if (body.size() > c_maxNotesBytes) {
        tooLarge = true;
        reply->abort();
      }
    };
    connect(reply.data(), &QNetworkReply::readyRead, reply.data(), readBody);
    bool timedOut = false;
    bool cancelled = false;
    waitForReply(
        reply.data(), remaining, [this]() { return isCancelled(); }, &timedOut, &cancelled);
    if (tooLarge || timedOut || cancelled || isCancelled() ||
        reply->error() != QNetworkReply::NoError) {
      return {};
    }
    readBody();
    if (tooLarge) {
      return {};
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200) {
      return m_source == Source::GitHub ? githubReleaseNotes(body, p_info.releaseUrl)
                                        : giteeReleaseNotes(body, p_info.latestVersion);
    }
    const QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if ((status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
        !location.isValid() || location.isEmpty()) {
      return {};
    }
    url = url.resolved(location);
  }
  return {};
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

    if (info.updateAvailable) {
      info.releaseNotes = fetchReleaseNotes(nam, info);
    }
    if (isCancelled()) {
      fail(tr("Cancelled."));
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
