// FolderBundleImporter — see the header for the contract.
//
// Mirrors FolderSharePackager's filesystem discipline in the opposite
// direction: non-following enumeration, strict vx.json validation (shared with
// the share side through FolderMetadataValidator), chunked copy-with-hash into
// a staging area, staged-tree verification, then a single commit callback.
//
// The one structural difference is WHERE the staging lives. The share side
// stages beside the user's chosen destination; the import side stages inside
// the notebook's own reserved metadata directory
// (<root>/vx_notebook/vx_import/<uuid>/), because that is the only location
// guaranteed to be on the same volume as BOTH rename targets (content tree and
// metadata tree) while staying out of the user-visible tree.

#include "folderbundleimporter.h"

#include "foldermetadatavalidator.h"
#include "foldersharepackager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>
#include <QUuid>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

Q_LOGGING_CATEGORY(folderImportLog, "vnote.folderimport")

const char *const kReservedPackageDir = "vx_notebook";
const char *const kContentsDir = "contents";
const char *const kImportStagingDir = "vx_import";
const char *const kStagedContentDir = "content";
const char *const kStagedMetadataDir = "metadata";

// 256 KiB copy/hash chunk, matching FolderSharePackager: large enough to keep
// syscall overhead down, small enough that cancellation stays responsive.
constexpr qint64 kChunkBytes = 256 * 1024;

// Upper bound on name-collision suffixes walked before giving up.
constexpr int kMaxNameAttempts = 1000;

using CancelFn = std::function<bool()>;
using ProgressFn = std::function<void(qint64)>;

bool cancelled(const CancelFn &p_isCancelled) { return p_isCancelled && p_isCancelled(); }

void markHidden(const QString &p_path) {
#ifdef Q_OS_WIN
  ::SetFileAttributesW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(p_path).utf16()),
                       FILE_ATTRIBUTE_HIDDEN);
#else
  Q_UNUSED(p_path);
#endif
}

QString foldForCompare(const QString &p_name, bool p_caseSensitive) {
  return p_caseSensitive ? p_name : p_name.toLower();
}

struct TreeEntry {
  QString rel; // '/'-separated, relative to the enumerated root
  bool isDir = false;
  qint64 size = 0;
};

// Non-following recursive walk. Rejects symlinks / reparse points / special
// (non-regular, non-directory) entries and unreadable nodes outright: a bundle
// carrying one is not something we can faithfully transplant, and following it
// would let the import pull in an arbitrary external tree.
//
// The ROOT itself is checked too (when p_relPrefix is empty), otherwise the
// bundle's own folder could BE a junction to somewhere else entirely.
bool enumerateTree(const QString &p_root, const QString &p_relPrefix, QVector<TreeEntry> *p_out,
                   const CancelFn &p_isCancelled, QString *p_error) {
  if (p_relPrefix.isEmpty() && FolderMetadataValidator::isLinkOrReparsePoint(p_root)) {
    *p_error = QObject::tr("Refusing to import: %1 is a symbolic link, junction or reparse point.")
                   .arg(p_root);
    return false;
  }

  QDir dir(p_root);
  if (!dir.exists()) {
    *p_error = QObject::tr("Bundle directory is missing: %1").arg(p_root);
    return false;
  }

  const QFileInfoList entries = dir.entryInfoList(
      QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);

  for (const QFileInfo &info : entries) {
    if (cancelled(p_isCancelled)) {
      return false;
    }
    const QString absolute = info.absoluteFilePath();
    const QString rel =
        p_relPrefix.isEmpty() ? info.fileName() : p_relPrefix + QLatin1Char('/') + info.fileName();

    if (FolderMetadataValidator::isLinkOrReparsePoint(absolute)) {
      *p_error =
          QObject::tr("Refusing to import: %1 is a symbolic link, junction or reparse point.")
              .arg(rel);
      return false;
    }

    if (info.isDir()) {
      TreeEntry entry;
      entry.rel = rel;
      entry.isDir = true;
      p_out->append(entry);
      if (!enumerateTree(absolute, rel, p_out, p_isCancelled, p_error)) {
        return false;
      }
      continue;
    }

    if (!info.isFile()) {
      *p_error = QObject::tr("Refusing to import: %1 is not a regular file.").arg(rel);
      return false;
    }
    if (!info.isReadable()) {
      *p_error = QObject::tr("Refusing to import: %1 is not readable.").arg(rel);
      return false;
    }

    TreeEntry entry;
    entry.rel = rel;
    entry.isDir = false;
    entry.size = info.size();
    p_out->append(entry);
  }
  return true;
}

FolderMetadataValidator::DirectoryLister makeDirectoryLister() {
  return [](const QString &p_root, QStringList *p_outDirs, QString *p_error) {
    QVector<TreeEntry> entries;
    if (!enumerateTree(p_root, QString(), &entries, CancelFn(), p_error)) {
      return false;
    }
    for (const TreeEntry &entry : entries) {
      if (entry.isDir) {
        p_outDirs->append(entry.rel);
      }
    }
    return true;
  };
}

qint64 totalFileBytes(const QVector<TreeEntry> &p_entries) {
  qint64 sum = 0;
  for (const TreeEntry &entry : p_entries) {
    if (!entry.isDir) {
      sum += entry.size;
    }
  }
  return sum;
}

// Reject entries that are DISTINCT in the bundle but would collide inside the
// notebook. A bundle produced on a case-sensitive filesystem can legitimately
// hold both "A.txt" and "a.txt"; landing them on a case-insensitive notebook
// volume would silently overwrite one with the other.
bool rejectDestinationNameCollisions(const QVector<TreeEntry> &p_entries, bool p_caseSensitive,
                                     const QString &p_label, QString *p_error) {
  if (p_caseSensitive) {
    return true; // Distinct names stay distinct.
  }
  QHash<QString, QHash<QString, QString>> seen; // parent -> folded -> original
  for (const TreeEntry &entry : p_entries) {
    const int slash = entry.rel.lastIndexOf(QLatin1Char('/'));
    const QString parent = slash < 0 ? QString() : entry.rel.left(slash);
    const QString name = slash < 0 ? entry.rel : entry.rel.mid(slash + 1);
    const QString folded = name.toLower();
    auto &level = seen[parent];
    auto it = level.constFind(folded);
    if (it != level.constEnd() && it.value() != name) {
      *p_error = QObject::tr("\"%1\" and \"%2\" in %3 would collide in this notebook, whose "
                             "filesystem does not distinguish letter case.")
                     .arg(it.value(), name, p_label);
      return false;
    }
    level.insert(folded, name);
  }
  return true;
}

bool readJsonObject(const QString &p_path, QJsonObject *p_out, QString *p_error) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    *p_error = QObject::tr("Cannot read %1").arg(p_path);
    return false;
  }
  const QByteArray raw = file.readAll();
  file.close();

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    *p_error = QObject::tr("Malformed folder metadata: %1").arg(p_path);
    return false;
  }
  *p_out = doc.object();
  return true;
}

bool writeJsonObject(const QString &p_path, const QJsonObject &p_object, QString *p_error) {
  QFile file(p_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    *p_error = QObject::tr("Cannot write %1").arg(p_path);
    return false;
  }
  const QByteArray raw = QJsonDocument(p_object).toJson(QJsonDocument::Indented);
  if (file.write(raw) != raw.size() || !file.flush()) {
    *p_error = QObject::tr("Write failed for %1").arg(p_path);
    return false;
  }
  file.close();
  return true;
}

// ---------------------------------------------------------------------------
// Bundle layout
// ---------------------------------------------------------------------------

// A bundle has exactly ONE non-"vx_notebook" child directory, whose name is the
// folder name, plus the matching metadata directory. There is no manifest and
// no vx_notebook/config.json, so the layout IS the contract.
bool resolveBundleLayout(const QString &p_bundlePath, QString *p_outFolderName,
                         QString *p_outContentRoot, QString *p_outMetadataRoot, QString *p_error) {
  const QFileInfo bundleInfo(p_bundlePath);
  if (!bundleInfo.exists() || !bundleInfo.isDir()) {
    *p_error = QObject::tr("The selected path is not a folder.");
    return false;
  }
  if (FolderMetadataValidator::isLinkOrReparsePoint(p_bundlePath)) {
    *p_error = QObject::tr("Refusing to import: %1 is a symbolic link, junction or reparse point.")
                   .arg(p_bundlePath);
    return false;
  }

  const QFileInfoList children =
      QDir(p_bundlePath)
          .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                         QDir::Name);

  QStringList candidates;
  bool hasMetadataDir = false;
  for (const QFileInfo &child : children) {
    if (child.fileName() == QLatin1String(kReservedPackageDir)) {
      hasMetadataDir = child.isDir();
      continue;
    }
    if (child.isDir()) {
      candidates.append(child.fileName());
    }
  }

  if (!hasMetadataDir) {
    *p_error = QObject::tr("This folder is not a VNote share bundle: it has no \"%1\" directory.")
                   .arg(QLatin1String(kReservedPackageDir));
    return false;
  }
  if (candidates.isEmpty()) {
    *p_error = QObject::tr("This folder is not a VNote share bundle: it contains no folder to "
                           "import.");
    return false;
  }
  if (candidates.size() > 1) {
    *p_error = QObject::tr("This folder is not a VNote share bundle: it contains more than one "
                           "folder.");
    return false;
  }

  const QString folderName = candidates.first();
  if (!FolderMetadataValidator::isSafeChildName(folderName)) {
    *p_error = QObject::tr("The bundle's folder name is not usable: %1").arg(folderName);
    return false;
  }

  const QString metadataRoot = p_bundlePath + QLatin1Char('/') +
                               QLatin1String(kReservedPackageDir) + QLatin1Char('/') +
                               QLatin1String(kContentsDir) + QLatin1Char('/') + folderName;
  if (!QFileInfo::exists(metadataRoot + QLatin1Char('/') +
                         FolderMetadataValidator::folderConfigFileName())) {
    *p_error = QObject::tr("This folder is not a VNote share bundle: the metadata for \"%1\" is "
                           "missing.")
                   .arg(folderName);
    return false;
  }

  *p_outFolderName = folderName;
  *p_outContentRoot = p_bundlePath + QLatin1Char('/') + folderName;
  *p_outMetadataRoot = metadataRoot;
  return true;
}

// Collects every folder and file id in the bundle's metadata subtree into ONE
// namespace, because a folder id colliding with an existing FILE id is just as
// destructive as a same-kind collision (vxcore's `uuid UNIQUE` is per-table).
bool collectBundleIds(const QString &p_metadataDir, QStringList *p_outIds, int *p_outFileCount,
                      int *p_outFolderCount, QString *p_error) {
  QJsonObject config;
  if (!readJsonObject(p_metadataDir + QLatin1Char('/') +
                          FolderMetadataValidator::folderConfigFileName(),
                      &config, p_error)) {
    return false;
  }

  p_outIds->append(config.value(QLatin1String(vxcore::kJsonKeyId)).toString());

  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
    p_outIds->append(value.toObject().value(QLatin1String(vxcore::kJsonKeyId)).toString());
    ++(*p_outFileCount);
  }

  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
    const QString name = value.toString();
    ++(*p_outFolderCount);
    if (!collectBundleIds(p_metadataDir + QLatin1Char('/') + name, p_outIds, p_outFileCount,
                          p_outFolderCount, p_error)) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Copy / hash / verify
// ---------------------------------------------------------------------------

bool copyFileChunked(const QString &p_src, const QString &p_dst, QByteArray *p_outHash,
                     const CancelFn &p_isCancelled, const ProgressFn &p_progress,
                     QString *p_error) {
  QFile in(p_src);
  if (!in.open(QIODevice::ReadOnly)) {
    *p_error = QObject::tr("Cannot read %1").arg(p_src);
    return false;
  }
  QFile out(p_dst);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    *p_error = QObject::tr("Cannot write %1").arg(p_dst);
    return false;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunkBytes));

  for (;;) {
    if (cancelled(p_isCancelled)) {
      return false;
    }
    const qint64 read = in.read(buffer.data(), kChunkBytes);
    if (read < 0) {
      *p_error = QObject::tr("Read failed for %1").arg(p_src);
      return false;
    }
    if (read == 0) {
      break;
    }
    if (out.write(buffer.constData(), read) != read) {
      *p_error = QObject::tr("Write failed for %1").arg(p_dst);
      return false;
    }
    hash.addData(QByteArray::fromRawData(buffer.constData(), static_cast<int>(read)));
    if (p_progress) {
      p_progress(read);
    }
  }

  if (!out.flush()) {
    *p_error = QObject::tr("Flush failed for %1").arg(p_dst);
    return false;
  }
  out.close();
  in.close();
  *p_outHash = hash.result();
  return true;
}

bool hashFileChunked(const QString &p_path, QByteArray *p_outHash, const CancelFn &p_isCancelled,
                     const ProgressFn &p_progress, QString *p_error) {
  QFile in(p_path);
  if (!in.open(QIODevice::ReadOnly)) {
    *p_error = QObject::tr("Cannot read %1").arg(p_path);
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunkBytes));
  for (;;) {
    if (cancelled(p_isCancelled)) {
      return false;
    }
    const qint64 read = in.read(buffer.data(), kChunkBytes);
    if (read < 0) {
      *p_error = QObject::tr("Read failed for %1").arg(p_path);
      return false;
    }
    if (read == 0) {
      break;
    }
    hash.addData(QByteArray::fromRawData(buffer.constData(), static_cast<int>(read)));
    if (p_progress) {
      p_progress(read);
    }
  }
  *p_outHash = hash.result();
  return true;
}

bool copyTree(const QString &p_srcRoot, const QString &p_dstRoot,
              const QVector<TreeEntry> &p_entries, QHash<QString, QByteArray> *p_outHashes,
              const CancelFn &p_isCancelled, const ProgressFn &p_progress, QString *p_error) {
  QDir dir;
  if (!dir.mkpath(p_dstRoot)) {
    *p_error = QObject::tr("Cannot create %1").arg(p_dstRoot);
    return false;
  }

  // Directories first, so an EMPTY directory in the bundle is preserved.
  for (const TreeEntry &entry : p_entries) {
    if (!entry.isDir) {
      continue;
    }
    if (cancelled(p_isCancelled)) {
      return false;
    }
    if (!dir.mkpath(p_dstRoot + QLatin1Char('/') + entry.rel)) {
      *p_error = QObject::tr("Cannot create %1").arg(p_dstRoot + QLatin1Char('/') + entry.rel);
      return false;
    }
  }

  for (const TreeEntry &entry : p_entries) {
    if (entry.isDir) {
      continue;
    }
    if (cancelled(p_isCancelled)) {
      return false;
    }
    QByteArray hash;
    if (!copyFileChunked(p_srcRoot + QLatin1Char('/') + entry.rel,
                         p_dstRoot + QLatin1Char('/') + entry.rel, &hash, p_isCancelled, p_progress,
                         p_error)) {
      return false;
    }
    p_outHashes->insert(entry.rel, hash);
  }
  return true;
}

// Verify the STAGED tree against the bundle inventory and the hashes recorded
// while copying. This is the only check that covers unindexed files, hidden
// files and empty directories; the metadata validator only knows about INDEXED
// content.
//
// Timestamps are deliberately not compared: the copy does not preserve mtimes.
// Identity is carried by the relative-path set, the entry type, the size and
// the SHA-256 of the bytes.
bool verifyStagedTree(const QString &p_stagedRoot, const QVector<TreeEntry> &p_inventory,
                      const QHash<QString, QByteArray> &p_hashes, const CancelFn &p_isCancelled,
                      const ProgressFn &p_progress, QString *p_error) {
  QVector<TreeEntry> staged;
  if (!enumerateTree(p_stagedRoot, QString(), &staged, p_isCancelled, p_error)) {
    return false;
  }

  if (staged.size() != p_inventory.size()) {
    *p_error = QObject::tr("The copied folder is incomplete.");
    return false;
  }

  QHash<QString, TreeEntry> expected;
  expected.reserve(p_inventory.size());
  for (const TreeEntry &entry : p_inventory) {
    expected.insert(entry.rel, entry);
  }

  for (const TreeEntry &entry : staged) {
    auto it = expected.constFind(entry.rel);
    if (it == expected.constEnd() || it->isDir != entry.isDir ||
        (!entry.isDir && it->size != entry.size)) {
      *p_error = QObject::tr("The copied folder is incomplete or corrupted (%1).").arg(entry.rel);
      return false;
    }
  }

  for (const TreeEntry &entry : staged) {
    if (entry.isDir) {
      continue;
    }
    if (cancelled(p_isCancelled)) {
      return false;
    }
    QByteArray hash;
    if (!hashFileChunked(p_stagedRoot + QLatin1Char('/') + entry.rel, &hash, p_isCancelled,
                         p_progress, p_error)) {
      return false;
    }
    if (hash != p_hashes.value(entry.rel)) {
      *p_error = QObject::tr("The copied folder is corrupted (%1).").arg(entry.rel);
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Destination name selection
// ---------------------------------------------------------------------------

// Every name the imported folder must not collide with, across all FOUR
// namespaces: what is physically present in the destination content directory
// and destination metadata directory, plus what the destination parent's
// vx.json already indexes as a folder or a file. Checking only the index would
// miss an unindexed directory already sitting on disk, and checking only disk
// would miss an indexed-but-missing node.
bool collectTakenNames(const QString &p_destContentRoot, const QString &p_destMetadataRoot,
                       bool p_caseSensitive, QSet<QString> *p_out, QString *p_error) {
  const auto addEntries = [&](const QString &p_dir) {
    const QFileInfoList entries = QDir(p_dir).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
      p_out->insert(foldForCompare(info.fileName(), p_caseSensitive));
    }
  };
  addEntries(p_destContentRoot);
  addEntries(p_destMetadataRoot);

  QJsonObject parentConfig;
  if (!readJsonObject(p_destMetadataRoot + QLatin1Char('/') +
                          FolderMetadataValidator::folderConfigFileName(),
                      &parentConfig, p_error)) {
    return false;
  }
  for (const QJsonValue &value :
       parentConfig.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
    p_out->insert(foldForCompare(value.toString(), p_caseSensitive));
  }
  for (const QJsonValue &value :
       parentConfig.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
    p_out->insert(foldForCompare(
        value.toObject().value(QLatin1String(vxcore::kJsonKeyName)).toString(), p_caseSensitive));
  }
  return true;
}

QString pickAvailableName(const QString &p_desired, const QSet<QString> &p_taken,
                          bool p_caseSensitive) {
  if (!p_taken.contains(foldForCompare(p_desired, p_caseSensitive))) {
    return p_desired;
  }
  for (int attempt = 2; attempt <= kMaxNameAttempts; ++attempt) {
    const QString candidate = QStringLiteral("%1 (%2)").arg(p_desired).arg(attempt);
    if (!p_taken.contains(foldForCompare(candidate, p_caseSensitive))) {
      return candidate;
    }
  }
  return QString();
}

} // namespace

// ---------------------------------------------------------------------------
// FolderBundleImporter
// ---------------------------------------------------------------------------

QString FolderBundleImporter::reservedPackageDirName() {
  return QLatin1String(kReservedPackageDir);
}

FolderBundleImporter::Inspection FolderBundleImporter::inspect(const QString &p_bundlePath) {
  Inspection inspection;

  QString folderName;
  QString contentRoot;
  QString metadataRoot;
  QString error;
  if (!resolveBundleLayout(p_bundlePath, &folderName, &contentRoot, &metadataRoot, &error)) {
    inspection.m_message = error;
    return inspection;
  }

  // Validate under case-INSENSITIVE folding: the preview must not promise a
  // bundle is importable when the eventual destination folds case. The real
  // run re-validates against the actual destination's probed behavior.
  if (!FolderMetadataValidator::validateMetadataSubtree(metadataRoot, contentRoot, folderName,
                                                        false, makeDirectoryLister(), &error)) {
    inspection.m_message = error;
    return inspection;
  }

  QStringList ids;
  int fileCount = 0;
  int folderCount = 0;
  if (!collectBundleIds(metadataRoot, &ids, &fileCount, &folderCount, &error)) {
    inspection.m_message = error;
    return inspection;
  }

  inspection.m_valid = true;
  inspection.m_folderName = folderName;
  inspection.m_fileCount = fileCount;
  inspection.m_subfolderCount = folderCount;
  inspection.m_message =
      QObject::tr("%1 — %2 notes, %3 subfolders").arg(folderName).arg(fileCount).arg(folderCount);
  return inspection;
}

FolderBundleImporter::Result FolderBundleImporter::run(const Request &p_request,
                                                       const Callbacks &p_callbacks) {
  Result result;

  const CancelFn isCancelled = p_callbacks.m_isCancelled;
  auto setPhase = [&p_callbacks](Phase p_phase) {
    if (p_callbacks.m_phaseChanged) {
      p_callbacks.m_phaseChanged(p_phase);
    }
  };
  auto fail = [&result](const QString &p_message) {
    result.m_status = Status::Failed;
    result.m_errorMessage = p_message;
    qCWarning(folderImportLog) << "folder import failed:" << p_message;
    return result;
  };
  auto cancel = [&result]() {
    result.m_status = Status::Cancelled;
    return result;
  };

  QString error;

  // ---- 1. Preflight -------------------------------------------------------
  setPhase(Phase::Validating);

  if (!p_callbacks.m_commit) {
    return fail(QObject::tr("Internal error: no commit handler was supplied."));
  }
  if (!p_callbacks.m_collectNodeIds) {
    return fail(QObject::tr("Internal error: no id oracle was supplied."));
  }

  QString bundleFolderName;
  QString bundleContentRoot;
  QString bundleMetadataRoot;
  if (!resolveBundleLayout(p_request.m_bundlePath, &bundleFolderName, &bundleContentRoot,
                           &bundleMetadataRoot, &error)) {
    return fail(error);
  }

  // Probe the DESTINATION rather than assuming a platform default: a
  // case-sensitive volume can be mounted on Windows and vice versa. Reuses the
  // share side's probe so both features agree on what "collides" means.
  const bool caseSensitive =
      FolderSharePackager::destinationIsCaseSensitive(p_request.m_destContentRoot);

  QVector<TreeEntry> contentEntries;
  QVector<TreeEntry> metadataEntries;
  if (!enumerateTree(bundleContentRoot, QString(), &contentEntries, isCancelled, &error)) {
    return cancelled(isCancelled) ? cancel() : fail(error);
  }
  if (!enumerateTree(bundleMetadataRoot, QString(), &metadataEntries, isCancelled, &error)) {
    return cancelled(isCancelled) ? cancel() : fail(error);
  }
  if (!rejectDestinationNameCollisions(contentEntries, caseSensitive, bundleFolderName, &error) ||
      !rejectDestinationNameCollisions(metadataEntries, caseSensitive,
                                       FolderMetadataValidator::folderConfigFileName(), &error)) {
    return fail(error);
  }
  if (!FolderMetadataValidator::validateMetadataSubtree(bundleMetadataRoot, bundleContentRoot,
                                                        bundleFolderName, caseSensitive,
                                                        makeDirectoryLister(), &error)) {
    return fail(error);
  }

  // ---- 2. Ids: the bundle's, and the notebook's ---------------------------
  QStringList bundleIdList;
  int fileCount = 0;
  int folderCount = 0;
  if (!collectBundleIds(bundleMetadataRoot, &bundleIdList, &fileCount, &folderCount, &error)) {
    return fail(error);
  }
  const QSet<QString> bundleIds(bundleIdList.begin(), bundleIdList.end());
  if (bundleIds.size() != bundleIdList.size()) {
    return fail(QObject::tr("The bundle contains duplicate node ids and cannot be imported."));
  }

  // Ids are preserved VERBATIM, so a collision is a hard failure: remapping
  // would silently break the very identity the import exists to keep.
  auto intersectsNotebook = [&p_callbacks, &bundleIds](QString *p_outError) -> int {
    QStringList existing;
    if (!p_callbacks.m_collectNodeIds(&existing, p_outError)) {
      return -1; // Fail CLOSED: "unknown" is never treated as "no collision".
    }
    for (const QString &id : existing) {
      if (bundleIds.contains(id)) {
        return 1;
      }
    }
    return 0;
  };

  const int collidesEarly = intersectsNotebook(&error);
  if (collidesEarly < 0) {
    return fail(error);
  }
  if (collidesEarly > 0) {
    return fail(QObject::tr("This folder is already in this notebook. Importing it again would "
                            "overwrite the existing notes, so nothing was changed."));
  }

  // ---- 3. Pick a free name ------------------------------------------------
  QSet<QString> takenNames;
  if (!collectTakenNames(p_request.m_destContentRoot, p_request.m_destMetadataRoot, caseSensitive,
                         &takenNames, &error)) {
    return fail(error);
  }
  const QString finalName = pickAvailableName(bundleFolderName, takenNames, caseSensitive);
  if (finalName.isEmpty()) {
    return fail(QObject::tr("Could not find a free name for \"%1\" in the destination.")
                    .arg(bundleFolderName));
  }

  // ---- 4. Staged copy -----------------------------------------------------
  const qint64 copyBytes = totalFileBytes(contentEntries) + totalFileBytes(metadataEntries);
  const qint64 totalBytes = copyBytes * 2; // copy pass + staged re-hash.
  qint64 doneBytes = 0;

  const QString stagingParent = p_request.m_notebookRoot + QLatin1Char('/') +
                                QLatin1String(kReservedPackageDir) + QLatin1Char('/') +
                                QLatin1String(kImportStagingDir);
  const QString stagingDir =
      stagingParent + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().mkpath(stagingDir)) {
    return fail(QObject::tr("Cannot create a staging folder inside the notebook."));
  }
  markHidden(stagingParent);

  bool committed = false;
  auto cleanupStaging = [&stagingDir, &stagingParent, &committed]() -> bool {
    if (committed || stagingDir.isEmpty()) {
      return true;
    }
    if (QDir(stagingDir).removeRecursively()) {
      // Drop the reserved staging parent too when it is now empty, so a
      // failed or cancelled import leaves the notebook byte-identical rather
      // than seeding an empty vx_import/ directory that sync would have to
      // learn to ignore. rmdir() is a no-op while another import is staging.
      QDir().rmdir(stagingParent);
      return true;
    }
    qCWarning(folderImportLog) << "folder import: could not remove the staging tree" << stagingDir;
    return false;
  };
  auto leftoverMessage = [&stagingDir]() {
    return QObject::tr("A temporary copy could not be removed and is still at %1. Delete it "
                       "manually.")
        .arg(QDir::toNativeSeparators(stagingDir));
  };
  auto failAndClean = [&fail, &cleanupStaging, &leftoverMessage](const QString &p_message) {
    if (!cleanupStaging()) {
      return fail(p_message + QLatin1Char('\n') + leftoverMessage());
    }
    return fail(p_message);
  };
  auto cancelAndClean = [&cancel, &fail, &cleanupStaging, &leftoverMessage]() {
    if (!cleanupStaging()) {
      // A cancellation that leaked a full copy is not a clean cancellation.
      return fail(leftoverMessage());
    }
    return cancel();
  };

  setPhase(Phase::Copying);
  if (p_callbacks.m_progress) {
    p_callbacks.m_progress(0, totalBytes);
  }

  // Coalesce progress emissions to whole-percent buckets: the caller's progress
  // dialog pumps the event loop, which is far too expensive to do per chunk.
  int lastPercent = -1;
  ProgressFn progress = [&](qint64 p_delta) {
    doneBytes += p_delta;
    const int percent = totalBytes > 0 ? static_cast<int>((doneBytes * 100) / totalBytes) : 0;
    if (percent != lastPercent) {
      lastPercent = percent;
      if (p_callbacks.m_progress) {
        p_callbacks.m_progress(doneBytes, totalBytes);
      }
    }
  };

  const QString stagedContent = stagingDir + QLatin1Char('/') + QLatin1String(kStagedContentDir);
  const QString stagedMetadata = stagingDir + QLatin1Char('/') + QLatin1String(kStagedMetadataDir);

  QHash<QString, QByteArray> contentHashes;
  QHash<QString, QByteArray> metadataHashes;

  if (!copyTree(bundleContentRoot, stagedContent, contentEntries, &contentHashes, isCancelled,
                progress, &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }
  if (p_request.m_failureInjection == QLatin1String("copy")) {
    return failAndClean(QObject::tr("Injected copy failure."));
  }
  if (!copyTree(bundleMetadataRoot, stagedMetadata, metadataEntries, &metadataHashes, isCancelled,
                progress, &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }

  // ---- 5. Verify ----------------------------------------------------------
  setPhase(Phase::Verifying);
  if (p_request.m_failureInjection == QLatin1String("verify")) {
    return failAndClean(QObject::tr("Injected verification failure."));
  }
  if (!verifyStagedTree(stagedContent, contentEntries, contentHashes, isCancelled, progress,
                        &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }
  if (!verifyStagedTree(stagedMetadata, metadataEntries, metadataHashes, isCancelled, progress,
                        &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }

  // ---- 6. Rewrite the staged root name if the folder was uniquified -------
  //
  // The ONLY field the import ever mutates. vxcore requires a folder's vx.json
  // "name" to equal its directory name; the id is left untouched, which is what
  // keeps the folder's identity, tags and history intact.
  if (finalName != bundleFolderName) {
    const QString stagedRootConfig =
        stagedMetadata + QLatin1Char('/') + FolderMetadataValidator::folderConfigFileName();
    QJsonObject rootConfig;
    if (!readJsonObject(stagedRootConfig, &rootConfig, &error)) {
      return failAndClean(error);
    }
    rootConfig[QLatin1String(vxcore::kJsonKeyName)] = finalName;
    if (!writeJsonObject(stagedRootConfig, rootConfig, &error)) {
      return failAndClean(error);
    }
  }

  // Announce the attach phase HERE, while a callback may still pump the event
  // loop. Everything after this point must be callback-free apart from the two
  // callbacks explicitly documented as non-pumping.
  setPhase(Phase::Attaching);
  if (p_request.m_failureInjection == QLatin1String("publish")) {
    return failAndClean(QObject::tr("Injected publish failure."));
  }

  // ---- 7. Final section: NOTHING below may pump the event loop -------------
  //
  // The progress callbacks above DID pump it, so the notebook may have changed
  // since the preflight: a note could have been created with one of the
  // bundle's ids, or the chosen name could have been taken. Re-check both.
  if (cancelled(isCancelled)) {
    return cancelAndClean();
  }

  const int collidesLate = intersectsNotebook(&error);
  if (collidesLate < 0) {
    return failAndClean(error);
  }
  if (collidesLate > 0) {
    return failAndClean(QObject::tr("This folder is already in this notebook. Importing it again "
                                    "would overwrite the existing notes, so nothing was "
                                    "changed."));
  }

  QSet<QString> takenNow;
  if (!collectTakenNames(p_request.m_destContentRoot, p_request.m_destMetadataRoot, caseSensitive,
                         &takenNow, &error)) {
    return failAndClean(error);
  }
  if (takenNow.contains(foldForCompare(finalName, caseSensitive))) {
    return failAndClean(
        QObject::tr("\"%1\" was created in the destination while the import was being prepared.")
            .arg(finalName));
  }

  // ---- 8. Commit ----------------------------------------------------------
  if (p_request.m_failureInjection == QLatin1String("attach")) {
    return failAndClean(QObject::tr("Injected attach failure."));
  }

  QString folderId;
  CommitRequest commit;
  commit.m_stagingDir = stagingDir;
  commit.m_destRelativePath = p_request.m_destRelativePath;
  commit.m_folderName = finalName;
  commit.m_outFolderId = &folderId;

  QString commitError;
  if (!p_callbacks.m_commit(commit, &commitError)) {
    // The importer owns rollback: whatever the commit published, it also
    // withdrew, so all that is left to clean is our own staging tree.
    return failAndClean(commitError.isEmpty() ? QObject::tr("The folder could not be added to the "
                                                            "notebook.")
                                              : commitError);
  }

  // The commit consumed the staging directory.
  committed = true;
  QDir().rmdir(stagingParent);

  result.m_status = Status::Succeeded;
  result.m_folderName = finalName;
  result.m_folderId = folderId;
  result.m_relativePath =
      (p_request.m_destRelativePath.isEmpty() || p_request.m_destRelativePath == QLatin1String("."))
          ? finalName
          : p_request.m_destRelativePath + QLatin1Char('/') + finalName;
  qCInfo(folderImportLog) << "folder bundle imported as" << result.m_relativePath;
  return result;
}
