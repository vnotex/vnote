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
  // Where releases are fetched from. The client contacts exactly ONE source at
  // a time and never follows a redirect from one source's hosts to the other's.
  enum class Source { GitHub, Gitee };

  static Source sourceFromString(const QString &p_source);
  static QString sourceToString(Source p_source);

  // Outcome of startDownload(). Only Started and NoPlan own a later terminal
  // signal for THAT call; Busy and Stale start nothing and emit nothing.
  enum class DownloadStart {
    Started,
    // A check or another download is already running. Reported BEFORE the plan
    // is even looked at, so "busy" always wins over "no plan": the running
    // check may be in the middle of writing that very plan.
    Busy,
    // No usable plan (never checked, or the plan was invalidated). failed() is
    // emitted, so this call does own its terminal signal.
    NoPlan,
    // A plan exists but is not the one the caller was offered -- a newer check
    // replaced it. Refused rather than silently downloading something else.
    Stale,
  };

  // Only these hosts are ever contacted, on https, with at most c_maxRedirects
  // hops and no HTTPS->HTTP downgrade. The list is per-source.
  static const QStringList &allowedHosts(Source p_source);
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

  // The release source in use. Pushed in by UpdateController from CoreConfig;
  // the service deliberately never reads config itself (see the class comment).
  // A change requested while a check or download is in flight is IGNORED (and
  // logged), so an in-flight plan cannot end up half-fetched from two origins.
  void setSource(Source p_source);
  Source source() const { return m_source; }

  // Human-facing releases page for the current source, used when a release
  // carries no usable page URL of its own.
  QUrl releasesPageUrl() const;

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

  // Forces the Microsoft Store (packaged app) detection used by
  // checkEligibility(): -1 auto-detect, 0 force not packaged, 1 force packaged.
  void testSetPackagedAppOverride(int p_state);

public slots:
  // Asynchronous. Emits checkFinished() or failed().
  void checkForUpdates();

  // Downloads and stages the plan produced by the last successful check.
  //
  // p_expectedTargetVersion is the version the CALLER was offered. It is
  // matched against the planned target so a UI element that outlived the check
  // it came from cannot start a different plan than the one it advertises;
  // pass an empty string only when no expectation is meaningful.
  //
  // Returns whether the operation was ACCEPTED. Only Started will ever produce
  // progress()/readyToApply()/failed() for this call, except NoPlan which
  // emits failed(). A caller that tracks which UI surface owns the transfer
  // must key off the return value rather than assuming the request landed.
  DownloadStart startDownload(const QString &p_expectedTargetVersion = QString());

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
  //
  // p_notFound, when non-null, is set to true ONLY for an HTTP 404 on the final
  // hop, so a caller can tell "the asset is not published" apart from every
  // other failure.
  bool fetchToMemory(QNetworkAccessManager &p_nam, const QUrl &p_url, QByteArray *p_out,
                     QString *p_error, qint64 p_maxBytes = 16LL * 1024 * 1024,
                     bool *p_notFound = nullptr);

  bool downloadToFile(QNetworkAccessManager &p_nam, const QUrl &p_url, const QString &p_destPath,
                      const QString &p_expectedSha, qint64 p_expectedSize, QString *p_error);

  QUrl assetUrl(const QString &p_version, const QString &p_assetName) const;
  QUrl manifestAssetUrl(const QString &p_version) const;
  QUrl manifestSignatureUrl(const QString &p_version) const;

  // Entry point of the release metadata API for the current source.
  QUrl apiLatestUrl() const;

  // Human-facing release page for p_tag. GitHub supplies html_url in the API
  // response; Gitee does not, so it is synthesized as
  // https://gitee.com/vnotex/vnote/releases/tag/v<tag> (verified against the
  // live v4.3.0 page -- the `tag/` segment is NOT present in the asset
  // download path).
  QString releasePageUrl(const QString &p_tag, const QString &p_htmlUrlFromApi) const;

  // Outcome of fetchVerifiedManifest.
  enum class ManifestFetch {
    Ok,
    // The MANIFEST ITSELF 404s: this release publishes no in-app update package
    // on the selected source. Never returned once manifest bytes are in hand --
    // a missing or unfetchable .minisig stays Error, which is the fail-closed
    // property.
    Absent,
    Error,
  };

  // Fetches a release manifest AND its detached minisign signature, verifies
  // the signature over the EXACT bytes received, and only then parses.
  //
  // Every manifest goes through here -- the target, each intermediate hop, and
  // the published base used for identity validation -- because each one steers
  // what gets downloaded and installed. Verifying only the target would leave
  // the chain walk attacker-controlled.
  bool fetchVerifiedManifest(QNetworkAccessManager &p_nam, const QString &p_version,
                             UpdateManifest *p_out, QString *p_error);

  // Tri-state form of the above. fetchVerifiedManifest() is the bool wrapper
  // used where Absent and Error are equally fatal (the chain walk falls back to
  // the full package either way).
  ManifestFetch fetchVerifiedManifestEx(QNetworkAccessManager &p_nam, const QString &p_version,
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

  Source m_source = Source::GitHub;

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

  // -1 auto, 0 force not packaged, 1 force packaged. See
  // testSetPackagedAppOverride().
  int m_packagedAppOverride = -1;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::UpdateInfo)

#endif // UPDATESERVICE_H
