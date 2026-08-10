#ifndef FOLDERMETADATAVALIDATOR_H
#define FOLDERMETADATAVALIDATOR_H

#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QJsonValue;

namespace vnotex {

// FolderMetadataValidator
//
// STRICT validation of a bundled notebook's `vx.json` metadata subtree.
//
// vxcore's `FolderConfig::FromJson()` is deliberately PERMISSIVE — every field
// is presence/type-guarded and silently defaulted — because it must load legacy
// notebooks. Anything that TRANSPLANTS a metadata subtree must not inherit that
// leniency: a record we could not fully understand is a record we cannot
// faithfully move. So both directions of the transplant use this validator:
//
//   - FolderSharePackager, before packaging a folder into a share bundle,
//   - FolderBundleImporter, before attaching a bundle to a notebook.
//
// The checks are keyed off the SAME shared constants vxcore serializes with
// (`<vxcore/notebook_json_keys.h>`), so writer and reader cannot drift.
//
// Deliberately free of Qt Widgets and of any service dependency: `core_services`
// links neither, and these are pure functions over paths and JSON.
class FolderMetadataValidator {
public:
  FolderMetadataValidator() = delete;

  // Lists every DIRECTORY under @p_root as '/'-separated relative paths.
  //
  // Injected rather than implemented here because the caller already owns a
  // hardened, non-following tree enumerator (rejecting symlinks, reparse points
  // and non-regular entries) and the orphan scan must use exactly the same
  // traversal rules as the rest of that caller's pipeline.
  using DirectoryLister =
      std::function<bool(const QString &p_root, QStringList *p_outDirs, QString *p_error)>;

  // The per-folder metadata file name ("vx.json").
  static QString folderConfigFileName();

  // A child name recorded in vx.json must be exactly one safe path component.
  static bool isSafeChildName(const QString &p_name);

  static bool isNonEmptyString(const QJsonValue &p_value);

  // vxcore serializes timestamps as int64_t. A fractional or out-of-range
  // number would have to be truncated or rejected downstream, so require a
  // genuinely integral one here.
  static bool isIntegralTimestamp(const QJsonValue &p_value);

  // Authoritative reparse-point / symlink test. QFileInfo::isSymLink() has
  // historically varied in how it reports NTFS junctions across Qt versions, so
  // the Windows path queries the attribute directly.
  static bool isLinkOrReparsePoint(const QString &p_path);

  // Validates one entry of a folder config's "files" array.
  static bool validateFileRecord(const QJsonValue &p_value, QString *p_outName, QString *p_outId,
                                 QString *p_error);

  // Validates one folder config and recurses into its listed children.
  //
  // p_visitedDirs collects every metadata directory reached THROUGH THE INDEX,
  // so the caller can reject orphans afterwards. p_seenIds collects every
  // folder/file id across the WHOLE subtree: vxcore's metadata store is keyed by
  // id, so a duplicate would collapse two records into one.
  static bool validateFolderMetadata(const QString &p_metadataDir, const QString &p_contentDir,
                                     const QString &p_expectedName, bool p_caseSensitive,
                                     QSet<QString> *p_visitedDirs, QSet<QString> *p_seenIds,
                                     QString *p_error);

  // Rejects metadata directories that hold a vx.json but were never reached
  // through the index walk, so a subtree can never carry records with no parent.
  static bool rejectOrphanMetadata(const QString &p_metadataRoot,
                                   const QSet<QString> &p_visitedDirs,
                                   const DirectoryLister &p_lister, QString *p_error);

  // Full strict validation of one (metadata, content) pair.
  static bool validateMetadataSubtree(const QString &p_metadataRoot, const QString &p_contentRoot,
                                      const QString &p_folderName, bool p_caseSensitive,
                                      const DirectoryLister &p_lister, QString *p_error);
};

} // namespace vnotex

#endif // FOLDERMETADATAVALIDATOR_H
