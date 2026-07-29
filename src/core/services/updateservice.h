#ifndef UPDATESERVICE_H
#define UPDATESERVICE_H

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

#include <atomic>

#include <core/updateinstaller.h>
#include <core/updatemanifest.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace vnotex {

// What a completed update check found.
struct UpdateInfo {
  // A newer version than the installed one is published.
  bool updateAvailable = false;

  QString currentVersion;
  QString latestVersion;

  // Release notes body and the human-facing release page, from the GitHub API.
  QString releaseNotes;
  QString releaseUrl;

  // Bytes that would actually be downloaded (delta chain or full package).
  qint64 downloadSize = 0;

  // True when the plan is a delta chain rather than the full package.
  bool isDelta = false;

  // Number of hops on the delta path (0 for the full package).
  int hopCount = 0;

  // False when this install cannot self-update in place; the caller should open
  // the releases page instead. ineligibleReason explains why.
  bool eligible = false;
  QString ineligibleReason;
};

// Orchestrates the CHECK -> PLAN -> DOWNLOAD -> STAGE half of the incremental
// updater. The APPLY half lives in UpdateInstaller and runs after every service
// (including this one) has been destroyed.
//
// Deliberately does NOT depend on ConfigMgr2: core_configs links core_services,
// so the dependency would be circular. The installed version is injected, and
// all policy that needs config (the skipped version, the check throttle, the
// "check on start" flag) lives in UpdateController.
class UpdateService : public QObject {
  Q_OBJECT

public:
  // Only these hosts are ever contacted, on https, with at most c_maxRedirects
  // hops and no HTTPS->HTTP downgrade.
  static const QStringList &allowedHosts();
  static constexpr int c_maxRedirects = 5;

  // Overall cap on a single download, as a sanity bound independent of the
  // manifest's own numbers.
  static constexpr qint64 c_maxArchiveBytes = 1024LL * 1024 * 1024;

  // Free space required beyond the archives plus their expansion.
  static constexpr qint64 c_freeSpaceHeadroom = 64LL * 1024 * 1024;

  UpdateService(const QString &p_installDir, const QString &p_currentVersion,
                QObject *p_parent = nullptr);
  ~UpdateService() override;

  // ---------------------------------------------------------------------
  // Eligibility
  // ---------------------------------------------------------------------
  struct Eligibility {
    bool eligible = false;
    QString reason;
  };

  // Runs the full eligibility gate: Windows + 64-bit, writable install dir, not
  // under Program Files, staging on the same volume, and the rename capability
  // probe. The probe result is cached for the process lifetime unless
  // p_forceProbe is set (apply preflight re-runs it).
  Eligibility checkEligibility(bool p_forceProbe = false) const;

  const QString &installDir() const { return m_installDir; }
  const QString &currentVersion() const { return m_currentVersion; }

  // From the local manifest.json when present, else derived from the build.
  QString variant() const;

  // Parsed <installDir>/manifest.json, or an invalid manifest when absent.
  UpdateManifest localManifest() const;

  // ---------------------------------------------------------------------
  // Pending update lifecycle
  // ---------------------------------------------------------------------

  // Revalidates <installDir>/.vnote-update/pending.json: schema, target version
  // strictly newer than the installed one, matching variant, and every staged
  // file still matching the target manifest. Invalid or superseded plans are
  // discarded silently. Returns true when a usable pending update remains.
  bool revalidatePending();

  bool hasPendingUpdate() const;
  QString pendingVersion() const;

  void discardPending();

  // Reads and CLEARS <installDir>/.vnote-update/result.json, which is how an
  // apply outcome crosses the restart (NotificationService is in-memory only).
  UpdateInstaller::StoredResult consumeStoredResult();

  // ---------------------------------------------------------------------
  // Test / local-E2E seam (unconditional, per ADR-6)
  // ---------------------------------------------------------------------

  // Redirects every request to a local server. Used by the manual end-to-end
  // harness described in the plan's Validation section. Empty restores GitHub.
  void testSetEndpointOverride(const QUrl &p_apiLatestUrl, const QUrl &p_assetBaseUrl);

  // Allows the override host through the allowlist. Only ever set alongside
  // testSetEndpointOverride.
  void testSetExtraAllowedHost(const QString &p_host);

public slots:
  // Asynchronous. Emits checkFinished() or failed().
  void checkForUpdates();

  // Downloads and stages the plan produced by the last successful check.
  // Emits progress(), then readyToApply() or failed().
  void startDownload();

  // Aborts an in-flight check or download. Staged bytes are left for a retry.
  void cancel();

signals:
  void checkFinished(const vnotex::UpdateInfo &p_info);

  // p_stage is a short, already-translated label; p_done/p_total are bytes for
  // downloads and item counts for verification passes.
  void progress(const QString &p_stage, qint64 p_done, qint64 p_total);

  void readyToApply(const QString &p_version);

  void failed(const QString &p_message);

private:
  struct Plan {
    bool valid = false;
    bool isDelta = false;

    // Oldest hop first. Empty for the full-package path.
    QStringList hopVersions;

    UpdateManifest target;

    // The verified base manifest for the delta path (invalid for the full path).
    UpdateManifest base;

    // version -> manifest, for every hop plus the base.
    QHash<QString, UpdateManifest> manifests;

    qint64 downloadSize = 0;
  };

  // --- Networking ---------------------------------------------------------
  // NOTE: QNetworkAccessManager is NOT thread-safe and belongs to the thread
  // that created it. Every network helper therefore takes the manager by
  // reference, and each worker task creates its OWN manager on its own stack.
  // There is deliberately no QNetworkAccessManager member.
  bool isHostAllowed(const QUrl &p_url) const;

  // Synchronous GET with redirect following, host allowlist, and a hard byte
  // cap. Runs on a WORKER thread only (it spins a nested event loop).
  bool fetchToMemory(QNetworkAccessManager &p_nam, const QUrl &p_url, QByteArray *p_out,
                     QString *p_error, qint64 p_maxBytes = 16LL * 1024 * 1024);

  bool downloadToFile(QNetworkAccessManager &p_nam, const QUrl &p_url, const QString &p_destPath,
                      const QString &p_expectedSha, qint64 p_expectedSize, QString *p_error);

  QUrl assetUrl(const QString &p_version, const QString &p_assetName) const;
  QUrl manifestAssetUrl(const QString &p_version) const;
  QUrl manifestSignatureUrl(const QString &p_version) const;

  // Fetches a release manifest AND its detached minisign signature, verifies
  // the signature over the EXACT bytes received, and only then parses.
  //
  // Every manifest goes through here -- the target, each intermediate hop, and
  // the published base used for identity validation -- because each one steers
  // what gets downloaded and installed. Verifying only the target would leave
  // the chain walk attacker-controlled.
  bool fetchVerifiedManifest(QNetworkAccessManager &p_nam, const QString &p_version,
                             UpdateManifest *p_out, QString *p_error);

  // --- Planning -----------------------------------------------------------
  Plan buildPlan(QNetworkAccessManager &p_nam, const UpdateManifest &p_target, QString *p_error);

  // --- Staging ------------------------------------------------------------
  bool stagePlan(QNetworkAccessManager &p_nam, const Plan &p_plan, QString *p_error);

  // p_allowPrune is true only on the DELTA path, where staged paths outside the
  // expectation are legitimately produced by intermediate hops and must be
  // pruned. On the full-package path an unexpected path is an error.
  bool verifyStagedTree(const Plan &p_plan, const QStringList &p_expectedChanged,
                        bool p_allowPrune, QString *p_error);

  bool hasEnoughFreeSpace(const Plan &p_plan, QString *p_error) const;

  void reportProgress(const QString &p_stage, qint64 p_done, qint64 p_total);
  void reportFailure(const QString &p_message);

  bool isCancelled() const { return m_cancelled.load(); }

  const QString m_installDir;
  const QString m_currentVersion;

  std::atomic<bool> m_cancelled{false};
  std::atomic<bool> m_busy{false};

  // Kept so the destructor can wait for an in-flight worker instead of letting
  // it run on against a destroyed object.
  QFuture<void> m_worker;

  // Result of the last successful check, consumed by startDownload().
  Plan m_plan;
  UpdateInfo m_lastInfo;

  mutable bool m_probeCached = false;
  mutable bool m_probeResult = false;

  QUrl m_apiLatestOverride;
  QUrl m_assetBaseOverride;
  QString m_extraAllowedHost;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::UpdateInfo)

#endif // UPDATESERVICE_H
