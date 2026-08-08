#ifndef UPDATESERVICE_H
#define UPDATESERVICE_H

#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QtGlobal>

#include <atomic>

class QNetworkAccessManager;
class QNetworkReply;

namespace vnotex {

// What a completed update check found.
struct UpdateInfo {
  // A newer version than the installed one is published.
  bool updateAvailable = false;

  QString currentVersion;
  QString latestVersion;

  // Release notes body and the human-facing release page, from the release API.
  QString releaseNotes;
  QString releaseUrl;
};

// Checks the selected forge for a newer release and reports what it found.
//
// That is the ENTIRE feature. VNote does not download, extract, execute,
// install or restart-to-apply anything, and it never writes outside its own
// configuration directory; when an update exists the user is sent to the
// release page to fetch it themselves. Do not reintroduce a downloader here
// without revisiting the "VNote never modifies its own install directory"
// invariant in the root AGENTS.md.
//
// Deliberately does NOT depend on ConfigMgr2: core_configs links core_services,
// so the dependency would be circular. The installed version is injected, and
// all policy that needs config (the skipped version, the check throttle, the
// "check on start" flag, the release source) lives in UpdateController.
class UpdateService : public QObject {
  Q_OBJECT

public:
  // Where releases are checked. The client contacts exactly ONE source at a
  // time and never follows a redirect from one source's hosts to the other's.
  enum class Source { GitHub, Gitee };

  static Source sourceFromString(const QString &p_source);
  static QString sourceToString(Source p_source);

  // Only these hosts are ever contacted, on https, with at most c_maxRedirects
  // hops and no HTTPS->HTTP downgrade. The list is per-source.
  static const QStringList &allowedHosts(Source p_source);
  static constexpr int c_maxRedirects = 5;

  // Cap on an API response body, enforced WHILE the body arrives.
  static constexpr qint64 c_maxApiResponseBytes = 16LL * 1024 * 1024;

  explicit UpdateService(const QString &p_currentVersion, QObject *p_parent = nullptr);
  ~UpdateService() override;

  // The release source in use. Pushed in by UpdateController from CoreConfig;
  // the service deliberately never reads config itself (see the class comment).
  // A change requested while a check is in flight is IGNORED (and logged).
  void setSource(Source p_source);
  Source source() const { return m_source; }

  // Human-facing releases page for the current source, used when a release
  // carries no usable page URL of its own.
  QUrl releasesPageUrl() const;

  const QString &currentVersion() const { return m_currentVersion; }

  // ---------------------------------------------------------------------
  // Test seams (unconditional, per ADR-6)
  // ---------------------------------------------------------------------

  // Redirects the release API at a local server. Empty restores production.
  void testSetEndpointOverride(const QUrl &p_apiLatestUrl);

  // Allows the override host through the allowlist. Only ever set alongside
  // testSetEndpointOverride.
  void testSetExtraAllowedHost(const QString &p_host);

public slots:
  // Asynchronous. Emits checkFinished() or failed() exactly once for an
  // ACCEPTED request.
  //
  // Returns whether the request was accepted: a call made while another check
  // is in flight is DROPPED and returns false, emitting nothing. A caller whose
  // presentation depends on which request produced a result (manual dialog vs
  // silent startup notification) MUST key off this return value -- assuming the
  // request landed would let a dropped call re-label the running check's
  // outcome.
  bool checkForUpdates();

  // Aborts an in-flight check. Polled, so teardown is never blocked for the
  // full request timeout.
  void cancel();

signals:
  void checkFinished(const vnotex::UpdateInfo &p_info);

  void failed(const QString &p_message);

private:
  // --- Networking ---------------------------------------------------------
  // NOTE: QNetworkAccessManager is NOT thread-safe and belongs to the thread
  // that created it. Every network helper therefore takes the manager by
  // reference, and the worker creates its OWN manager on its own stack. There
  // is deliberately no QNetworkAccessManager member.
  bool isHostAllowed(const QUrl &p_url) const;

  // Synchronous GET with redirect following, host allowlist, and a hard byte
  // cap that ABORTS the reply the moment it is crossed. Runs on a WORKER thread
  // only (it spins a nested event loop).
  bool fetchToMemory(QNetworkAccessManager &p_nam, const QUrl &p_url, QByteArray *p_out,
                     QString *p_error, qint64 p_maxBytes = c_maxApiResponseBytes);

  // Entry point of the release metadata API for the current source.
  QUrl apiLatestUrl() const;

  // Human-facing release page for p_tag.
  //
  // GitHub supplies html_url in the API response, but it is used only when it
  // is a valid URL on an allowlisted host (it is handed to QDesktopServices);
  // otherwise it falls back to a synthesized tag URL. Gitee supplies no
  // html_url at all, so the page is always synthesized as
  // https://gitee.com/vnotex/vnote/releases/tag/v<tag> (verified against the
  // live v4.3.0 page -- the `tag/` segment is NOT present in the asset download
  // path).
  QString releasePageUrl(const QString &p_tag, const QString &p_htmlUrlFromApi) const;

  void reportFailure(const QString &p_message);

  // Retains p_worker so the destructor can wait for it, pruning the futures
  // that have already finished.
  void trackWorker(const QFuture<void> &p_worker);

  // Waits for every outstanding worker. m_busy alone is NOT enough: it is
  // released by the worker itself, so a single stored QFuture could be replaced
  // by the next check while the previous worker is still unwinding.
  void waitForWorkers();

  bool isCancelled() const { return m_cancelled.load(); }

  const QString m_currentVersion;

  // Gitee by default: the majority of VNote's users reach it far more reliably
  // than GitHub. An explicitly persisted "github" is honored (see
  // CoreConfig::normalizeUpdateSource).
  Source m_source = Source::Gitee;

  std::atomic<bool> m_cancelled{false};
  std::atomic<bool> m_busy{false};

  // Every worker that has been started and may still be running. Guarded by
  // m_workerMutex; see waitForWorkers().
  QMutex m_workerMutex;
  QVector<QFuture<void>> m_workers;

  QUrl m_apiLatestOverride;
  QString m_extraAllowedHost;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::UpdateInfo)

#endif // UPDATESERVICE_H
