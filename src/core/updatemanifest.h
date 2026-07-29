#ifndef UPDATEMANIFEST_H
#define UPDATEMANIFEST_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace vnotex {

// One entry of a release manifest's files[] array.
//
// Paths are forward-slash, relative to the install root, and NEVER include
// "manifest.json" itself. See the "Manifest format" section of
// .kilo/plans/1785337074532-incremental-update-plan.md and the "Incremental
// Update" section of the root AGENTS.md.
struct UpdateManifestFile {
  // Install-root-relative, forward-slash path, exactly as published.
  QString path;

  // Uncompressed size in bytes.
  qint64 size = 0;

  // Lowercase hex SHA-256 of the file contents.
  QString sha256;

  bool operator==(const UpdateManifestFile &p_other) const {
    return path == p_other.path && size == p_other.size &&
           sha256.compare(p_other.sha256, Qt::CaseInsensitive) == 0;
  }
  bool operator!=(const UpdateManifestFile &p_other) const { return !(*this == p_other); }
};

// Optional "fullPackage" / "delta" blocks of a *release asset* manifest. The
// in-package manifest.json carries neither.
struct UpdateArchiveRef {
  // Release asset file name, e.g. "VNote-4.3.2-win64.delta.zip".
  QString asset;

  qint64 size = 0;

  QString sha256;

  // Only meaningful for the delta block: the version this delta applies on top
  // of. Empty for fullPackage.
  QString baseVersion;

  bool isValid() const { return !asset.isEmpty() && size > 0 && !sha256.isEmpty(); }
};

// Value type for both the in-package manifest.json and the published
// <name>.manifest.json release asset. Pure: no widgets, no network, no
// filesystem access. Lives in core_services so tests can link it.
class UpdateManifest {
public:
  // The only schema version this build understands.
  static constexpr int c_supportedSchema = 1;

  // Channel that is eligible as a delta base. Continuous builds are NEVER a
  // valid base (their contents are not reproducible from a published tag).
  static const QString &stableChannel();

  static const QString &continuousChannel();

  // The in-package manifest file name, excluded from files[] by construction.
  static const QString &manifestFileName();

  UpdateManifest() = default;

  // --- Parsing / serialization -------------------------------------------

  // Returns an invalid manifest (isValid() == false) on any structural problem:
  // unsupported/absent schema, missing version/variant/platform, malformed
  // files[], a path that fails normalizePath(), or a duplicate path (after
  // case-folding). p_error, when non-null, receives a human-readable reason.
  static UpdateManifest fromJson(const QJsonObject &p_obj, QString *p_error = nullptr);

  static UpdateManifest fromJsonBytes(const QByteArray &p_bytes, QString *p_error = nullptr);

  QJsonObject toJson() const;

  bool isValid() const { return m_valid; }

  // --- Accessors ----------------------------------------------------------

  int schema() const { return m_schema; }
  const QString &product() const { return m_product; }
  const QString &channel() const { return m_channel; }
  const QString &version() const { return m_version; }
  const QString &variant() const { return m_variant; }
  const QString &platform() const { return m_platform; }
  const QString &commit() const { return m_commit; }
  const QString &generatedAt() const { return m_generatedAt; }

  const UpdateArchiveRef &fullPackage() const { return m_fullPackage; }
  const UpdateArchiveRef &delta() const { return m_delta; }
  bool hasDelta() const { return m_delta.isValid() && !m_delta.baseVersion.isEmpty(); }

  // files[] in published order.
  const QVector<UpdateManifestFile> &files() const { return m_files; }

  // Case-folded path -> entry. Windows path comparison is case-insensitive;
  // the original casing is preserved inside the entry.
  const QHash<QString, UpdateManifestFile> &fileMap() const { return m_fileMap; }

  bool isStableChannel() const { return m_channel == stableChannel(); }

  // Sum of files[].size. This is the EXPANDED size, never a compressed size.
  qint64 totalExpandedSize() const;

  // Case-folded lookup. Returns false when absent.
  bool lookup(const QString &p_path, UpdateManifestFile *p_out = nullptr) const;

  // --- Diff / planning ----------------------------------------------------

  struct Diff {
    // Present in target but absent from base.
    QStringList added;
    // Present in both with a different sha256.
    QStringList changed;
    // Present in base but absent from target.
    QStringList removed;

    bool isEmpty() const { return added.isEmpty() && changed.isEmpty() && removed.isEmpty(); }
  };

  // All lists hold the TARGET's casing for added/changed and the BASE's casing
  // for removed, and are sorted for determinism.
  static Diff diff(const UpdateManifest &p_base, const UpdateManifest &p_target);

  // { p in target : p not in base || target[p].sha256 != base[p].sha256 }
  //
  // This is the authoritative "what must end up staged" set, computed from the
  // VERIFIED base and the FINAL target, deliberately ignoring intermediate hops
  // (a path changed by a hop and reverted by a later one is NOT in the set).
  // Returned in target casing, sorted.
  static QStringList expectedChanged(const UpdateManifest &p_base,
                                     const UpdateManifest &p_target);

  // The exact entry set a single hop's delta archive must contain: same formula
  // as expectedChanged() but for that hop's own base/target. Deletions are
  // derived, never stored, so they have no archive entries.
  static QStringList hopArchiveSet(const UpdateManifest &p_hopBase,
                                   const UpdateManifest &p_hopTarget);

  // Paths present in base but absent from target, in base casing, sorted.
  static QStringList deletions(const UpdateManifest &p_base, const UpdateManifest &p_target);

  // --- Chain resolution ---------------------------------------------------

  static constexpr int c_maxChainHops = 5;

  // Fraction of the target's expanded size above which the delta chain is
  // rejected in favor of a straight full-package download.
  static constexpr double c_maxChainSizeRatio = 0.6;

  enum class ChainStatus {
    Ok,
    // p_localVersion already equals the newest manifest's version.
    AlreadyCurrent,
    // Some manifest in the walk has no delta block.
    MissingDelta,
    // The chain did not reach p_localVersion within c_maxChainHops.
    TooManyHops,
    // baseVersion pointed at a version that is not in p_available.
    BrokenChain,
    // A manifest in the chain is not on the stable channel.
    NonStableBase,
    // Total delta bytes exceed c_maxChainSizeRatio * target expanded size.
    TooLarge,
    // Malformed input (invalid newest manifest, empty local version, ...).
    InvalidInput,
  };

  struct ChainResult {
    ChainStatus status = ChainStatus::InvalidInput;

    // Versions to apply, OLDEST HOP FIRST. Each entry is the version whose
    // delta archive must be downloaded and overlaid. Empty unless status == Ok.
    QStringList hopVersions;

    // Sum of the hops' delta archive sizes.
    qint64 totalDeltaSize = 0;

    bool isOk() const { return status == ChainStatus::Ok; }
  };

  // Walks p_newest.delta().baseVersion backwards through p_available (keyed by
  // version string) until p_localVersion is reached.
  //
  // p_available must contain every intermediate manifest INCLUDING p_newest.
  // Every manifest on the walk must be stable-channel; the size cap is measured
  // against p_newest.totalExpandedSize().
  static ChainResult resolveChain(const UpdateManifest &p_newest,
                                  const QString &p_localVersion,
                                  const QHash<QString, UpdateManifest> &p_available);

  // --- Identity validation ------------------------------------------------

  // True when the locally installed manifest is byte-for-byte equivalent to the
  // published manifest for the same version: version, variant, platform, commit
  // and the FULL files[] map must all match. This is what makes the local tree
  // a trustworthy delta base.
  //
  // p_error, when non-null, receives the first mismatch found.
  static bool validateBaseIdentity(const UpdateManifest &p_local,
                                   const UpdateManifest &p_published,
                                   QString *p_error = nullptr);

  // --- Path handling ------------------------------------------------------

  // Normalizes an archive/manifest path to the canonical install-root-relative
  // forward-slash form, or returns an empty string when the path is unsafe.
  //
  // Rejects: empty; absolute ("/x", "\\x"); drive-qualified ("C:/x", "C:x");
  // UNC ("//server/share"); Win32 device namespace ("\\\\?\\", "\\\\.\\"); any
  // "." or ".." segment; an empty segment (i.e. "a//b"); a segment that is a
  // reserved Windows device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or
  // without an extension); a segment with a trailing dot or space (Win32 strips
  // those, so "foo." and "foo" would alias); a segment containing a character
  // Win32 forbids (< > : " | ? *) or a control character; and a trailing slash.
  static QString normalizePath(const QString &p_path);

  // Case-folded key used for all path comparisons (Windows semantics).
  static QString pathKey(const QString &p_normalizedPath);

  // Build variant used when the local install has no manifest.json to read it
  // from. Qt6 builds ship as "win64"; the Qt5 build ships as "win64-windows7".
  static QString variantForBuild();

  // Reserved directory names that no archive entry may live under.
  static const QString &stagingDirName();  // ".vnote-update"
  static const QString &backupDirName();   // ".vnote-old"

  // True when p_normalizedPath is inside the staging or backup directory.
  static bool isReservedPath(const QString &p_normalizedPath);

private:
  static bool parseArchiveRef(const QJsonObject &p_obj, UpdateArchiveRef *p_out);

  bool m_valid = false;

  int m_schema = 0;
  QString m_product;
  QString m_channel;
  QString m_version;
  QString m_variant;
  QString m_platform;
  QString m_commit;
  QString m_generatedAt;

  QVector<UpdateManifestFile> m_files;
  QHash<QString, UpdateManifestFile> m_fileMap;

  UpdateArchiveRef m_fullPackage;
  UpdateArchiveRef m_delta;
};

} // namespace vnotex

#endif // UPDATEMANIFEST_H
