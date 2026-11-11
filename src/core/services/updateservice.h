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

  // Optional Markdown release notes and the human-facing release page.
  QString releaseNotes;
  QString releaseUrl;
};

// Checks the selected forge for a newer release and reports what it found.
//
// Only release metadata is fetched, in memory. This service never downloads
// release artifacts, writes files or installs anything; installation belongs
// exclusively to the explicit external updater. See the root update contract.
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

  // Redirects latest-release discovery at a local server. Empty restores production.
  void testSetEndpointOverride(const QUrl &p_latestReleaseUrl);

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

  // HEAD-only discovery on a worker. Stop at a validated stable tag redirect;
  // never request the tag page or an asset.
  bool findLatestRelease(QNetworkAccessManager &p_nam, UpdateInfo *p_info, QString *p_error);

  // Optional, bounded metadata GET after discovery, only when an update is available.
  QString fetchReleaseNotes(QNetworkAccessManager &p_nam, const UpdateInfo &p_info);

  QUrl latestReleaseUrl() const;

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

  QUrl m_latestReleaseOverride;
  QString m_extraAllowedHost;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::UpdateInfo)

#endif // UPDATESERVICE_H
