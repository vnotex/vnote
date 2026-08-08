// FolderSharePackager — see the header for the produced layout and contract.
//
// This file carries the whole filesystem discipline of the share feature:
// non-following enumeration, reparse-point rejection (roots AND ancestors),
// destination case-collision preflight, strict vx.json validation, chunked
// copy-with-hash, post-copy source revalidation, staged-tree verification, and
// an atomic single-rename publish.

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
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QVector>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

Q_LOGGING_CATEGORY(folderShareLog, "vnote.foldershare")

// The metadata directory name reserved at the package root. A legal NESTED
// notebook folder may legitimately be called "vx_notebook"; it just cannot
// occupy the package root beside the metadata tree, so such a selection is
// refused rather than silently mangled.
const char *const kReservedPackageDir = "vx_notebook";
const char *const kFolderConfigFile = "vx.json";
const char *const kBundleSuffix = "-bundle";

// 256 KiB copy/hash chunk: large enough to keep syscall overhead down, small
// enough that cancellation stays responsive on slow media.
constexpr qint64 kChunkBytes = 256 * 1024;

// Upper bound on published-name collisions walked before giving up. Guards
// against a pathological destination directory.
constexpr int kMaxNameAttempts = 1000;

// A rename can fail transiently while the target name is still free (an
// on-access virus scanner or the Windows search indexer holding a handle on the
// directory we just finished writing). Retry the same name this many times,
// spaced by kRenameRetryDelayMs, before moving on to the next suffix.
constexpr int kRenameRetries = 5;
constexpr unsigned long kRenameRetryDelayMs = 40;

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

// Authoritative reparse-point test. QFileInfo::isSymLink() has historically
// varied in how it reports NTFS junctions across Qt versions, so query the
// attribute directly on Windows.
bool isReparsePoint(const QString &p_path) {
#ifdef Q_OS_WIN
  const DWORD attrs = ::GetFileAttributesW(
      reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(p_path).utf16()));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false; // Does not exist: nothing to traverse.
  }
  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  const QFileInfo info(p_path);
  return info.exists() && info.isSymLink();
#endif
}

bool isLinkOrReparsePoint(const QString &p_path) {
  return isReparsePoint(p_path) || QFileInfo(p_path).isSymLink();
}

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

// A child name recorded in vx.json must be exactly one safe path component.
bool isSafeChildName(const QString &p_name) {
  if (p_name.isEmpty() || p_name == QLatin1String(".") || p_name == QLatin1String("..")) {
    return false;
  }
  if (p_name.contains(QLatin1Char('/')) || p_name.contains(QLatin1Char('\\'))) {
    return false;
  }
  return !p_name.contains(QLatin1Char(':'));
}

// ---------------------------------------------------------------------------
// Source inventory
// ---------------------------------------------------------------------------

struct TreeEntry {
  QString rel; // '/'-separated, relative to the enumerated root
  bool isDir = false;
  qint64 size = 0;
  qint64 mtimeMs = 0;
};

// Identity comparison used by the post-copy source revalidation.
//
// DIRECTORY timestamps are deliberately NOT compared. A directory's mtime moves
// whenever any child is created or removed, and Windows flushes it lazily, so a
// bare directory-mtime difference is not evidence that anything we copied
// changed. Real directory-level changes ARE caught: the entry SET must match
// exactly, so an added or removed child fails regardless.
bool operator==(const TreeEntry &p_a, const TreeEntry &p_b) {
  if (p_a.rel != p_b.rel || p_a.isDir != p_b.isDir) {
    return false;
  }
  if (p_a.isDir) {
    return true;
  }
  return p_a.size == p_b.size && p_a.mtimeMs == p_b.mtimeMs;
}

using CancelFn = std::function<bool()>;
using ProgressFn = std::function<void(qint64)>;

bool cancelled(const CancelFn &p_isCancelled) { return p_isCancelled && p_isCancelled(); }

// Non-following recursive walk. Rejects symlinks / reparse points / special
// (non-regular, non-directory) entries and unreadable nodes outright: a bundle
// that silently dropped or followed one would not be a faithful copy.
//
// The ROOT itself is checked too (when p_relPrefix is empty). Checking only the
// children would let the selected folder BE a junction to an external tree,
// which the walker would then happily traverse and copy.
bool enumerateTree(const QString &p_root, const QString &p_relPrefix, QVector<TreeEntry> *p_out,
                   const CancelFn &p_isCancelled, QString *p_error) {
  if (p_relPrefix.isEmpty() && isLinkOrReparsePoint(p_root)) {
    *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                   .arg(p_root);
    return false;
  }

  QDir dir(p_root);
  if (!dir.exists()) {
    *p_error = QObject::tr("Source directory is missing: %1").arg(p_root);
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

    if (isLinkOrReparsePoint(absolute)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(rel);
      return false;
    }

    if (info.isDir()) {
      TreeEntry entry;
      entry.rel = rel;
      entry.isDir = true;
      entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();
      p_out->append(entry);
      if (!enumerateTree(absolute, rel, p_out, p_isCancelled, p_error)) {
        return false;
      }
      continue;
    }

    if (!info.isFile()) {
      *p_error = QObject::tr("Refusing to share: %1 is not a regular file.").arg(rel);
      return false;
    }
    if (!info.isReadable()) {
      *p_error = QObject::tr("Refusing to share: %1 is not readable.").arg(rel);
      return false;
    }

    TreeEntry entry;
    entry.rel = rel;
    entry.isDir = false;
    entry.size = info.size();
    entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    p_out->append(entry);
  }
  return true;
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

// True when NO component of p_relativePath under p_root is a symlink / reparse
// point. vxcore proves the selected folder is indexed, but an ANCESTOR replaced
// by a junction would still resolve, so the whole chain is re-checked here
// where the authoritative GetFileAttributesW probe lives.
bool ancestorChainIsSafe(const QString &p_root, const QString &p_relativePath, QString *p_error) {
  QString current = QDir::cleanPath(p_root);
  if (isLinkOrReparsePoint(current)) {
    *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                   .arg(current);
    return false;
  }
  const QStringList parts =
      QDir::cleanPath(p_relativePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    current += QLatin1Char('/');
    current += part;
    if (isLinkOrReparsePoint(current)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(current);
      return false;
    }
  }
  return true;
}

// Reject entries that are DISTINCT in the source but would collide on the
// destination filesystem. A case-sensitive source can legitimately hold both
// "A.txt" and "a.txt"; copying them onto a case-insensitive destination would
// silently overwrite one with the other and publish an incomplete bundle.
// Checked per directory level, over the COMPLETE inventory (unindexed and
// hidden content included), not just the indexed metadata.
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
      *p_error = QObject::tr("\"%1\" and \"%2\" in %3 would collide on the destination "
                             "filesystem, which does not distinguish letter case.")
                     .arg(it.value(), name, p_label);
      return false;
    }
    level.insert(folded, name);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Strict vx.json validation
// ---------------------------------------------------------------------------
//
// FolderConfig::FromJson() is deliberately PERMISSIVE (it silently defaults or
// ignores missing / wrong-typed fields) because it must load legacy notebooks.
// A bundle must not inherit that leniency: a record we could not fully
// understand is a record we cannot faithfully transplant. Hence this separate
// strict validator, keyed off the SAME shared constants vxcore serializes with.

bool isNonEmptyString(const QJsonValue &p_value) {
  return p_value.isString() && !p_value.toString().isEmpty();
}

// vxcore serializes timestamps as int64_t. Accepting a fractional or
// out-of-range number here would let a bundle carry a value the importer must
// truncate or reject, so require a genuinely integral one.
bool isIntegralTimestamp(const QJsonValue &p_value) {
  if (!p_value.isDouble()) {
    return false;
  }
  const double raw = p_value.toDouble();
  if (!std::isfinite(raw)) {
    return false;
  }
  if (raw != std::floor(raw)) {
    return false;
  }
  // Beyond 2^53 a double no longer represents consecutive integers exactly.
  constexpr double kMaxExact = 9007199254740992.0;
  return raw >= -kMaxExact && raw <= kMaxExact;
}

bool validateFileRecord(const QJsonValue &p_value, QString *p_outName, QString *p_outId,
                        QString *p_error) {
  if (!p_value.isObject()) {
    *p_error = QObject::tr("A file record in vx.json is not an object.");
    return false;
  }
  const QJsonObject record = p_value.toObject();

  if (!isNonEmptyString(record.value(QLatin1String(vxcore::kJsonKeyId)))) {
    *p_error = QObject::tr("A file record is missing a valid \"id\".");
    return false;
  }
  if (!isNonEmptyString(record.value(QLatin1String(vxcore::kJsonKeyName)))) {
    *p_error = QObject::tr("A file record is missing a valid \"name\".");
    return false;
  }
  const QString name = record.value(QLatin1String(vxcore::kJsonKeyName)).toString();

  if (!isIntegralTimestamp(record.value(QLatin1String(vxcore::kJsonKeyCreatedUtc))) ||
      !isIntegralTimestamp(record.value(QLatin1String(vxcore::kJsonKeyModifiedUtc)))) {
    *p_error = QObject::tr("File record \"%1\" has non-numeric timestamps.").arg(name);
    return false;
  }
  if (!record.value(QLatin1String(vxcore::kJsonKeyMetadata)).isObject()) {
    *p_error = QObject::tr("File record \"%1\" has a non-object \"metadata\".").arg(name);
    return false;
  }
  if (!record.value(QLatin1String(vxcore::kJsonKeyTags)).isArray()) {
    *p_error = QObject::tr("File record \"%1\" has a non-array \"tags\".").arg(name);
    return false;
  }
  for (const QJsonValue &tag : record.value(QLatin1String(vxcore::kJsonKeyTags)).toArray()) {
    if (!tag.isString()) {
      *p_error = QObject::tr("File record \"%1\" has a non-string tag entry.").arg(name);
      return false;
    }
  }
  // "attachments" is OPTIONAL: the canonical serializer omits it when empty.
  // When present it must be an array of strings.
  if (record.contains(QLatin1String(vxcore::kJsonKeyAttachments))) {
    const QJsonValue attachments = record.value(QLatin1String(vxcore::kJsonKeyAttachments));
    if (!attachments.isArray()) {
      *p_error = QObject::tr("File record \"%1\" has a non-array \"attachments\".").arg(name);
      return false;
    }
    for (const QJsonValue &item : attachments.toArray()) {
      if (!item.isString()) {
        *p_error = QObject::tr("File record \"%1\" has a non-string attachment entry.").arg(name);
        return false;
      }
    }
  }

  *p_outName = name;
  *p_outId = record.value(QLatin1String(vxcore::kJsonKeyId)).toString();
  return true;
}

// Validates one folder config and recurses into its listed children.
// p_visitedDirs collects every metadata directory reached through the index so
// the caller can reject orphan metadata afterwards. p_seenIds collects every
// folder/file id across the WHOLE subtree: vxcore's metadata store is keyed by
// id, so a duplicate would collapse two records into one on import.
bool validateFolderMetadata(const QString &p_metadataDir, const QString &p_contentDir,
                            const QString &p_expectedName, bool p_caseSensitive,
                            QSet<QString> *p_visitedDirs, QSet<QString> *p_seenIds,
                            QString *p_error) {
  const QString configPath = p_metadataDir + QLatin1Char('/') + QLatin1String(kFolderConfigFile);

  QFile file(configPath);
  if (!file.exists()) {
    *p_error = QObject::tr("Missing folder metadata: %1").arg(configPath);
    return false;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    *p_error = QObject::tr("Cannot read folder metadata: %1").arg(configPath);
    return false;
  }
  const QByteArray raw = file.readAll();
  file.close();

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    *p_error = QObject::tr("Malformed folder metadata: %1").arg(configPath);
    return false;
  }
  const QJsonObject config = doc.object();

  if (!isNonEmptyString(config.value(QLatin1String(vxcore::kJsonKeyId)))) {
    *p_error = QObject::tr("Folder metadata is missing a valid \"id\": %1").arg(configPath);
    return false;
  }
  if (!isNonEmptyString(config.value(QLatin1String(vxcore::kJsonKeyName)))) {
    *p_error = QObject::tr("Folder metadata is missing a valid \"name\": %1").arg(configPath);
    return false;
  }
  if (config.value(QLatin1String(vxcore::kJsonKeyName)).toString() != p_expectedName) {
    *p_error = QObject::tr("Folder metadata name does not match its directory: %1").arg(configPath);
    return false;
  }
  {
    const QString folderId = config.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (p_seenIds->contains(folderId)) {
      *p_error = QObject::tr("Duplicate id \"%1\" in %2").arg(folderId, configPath);
      return false;
    }
    p_seenIds->insert(folderId);
  }
  if (!isIntegralTimestamp(config.value(QLatin1String(vxcore::kJsonKeyCreatedUtc))) ||
      !isIntegralTimestamp(config.value(QLatin1String(vxcore::kJsonKeyModifiedUtc)))) {
    *p_error = QObject::tr("Folder metadata has non-numeric timestamps: %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyMetadata)).isObject()) {
    *p_error = QObject::tr("Folder metadata has a non-object \"metadata\": %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyFiles)).isArray()) {
    *p_error = QObject::tr("Folder metadata has a non-array \"files\": %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyFolders)).isArray()) {
    *p_error = QObject::tr("Folder metadata has a non-array \"folders\": %1").arg(configPath);
    return false;
  }

  p_visitedDirs->insert(QDir::cleanPath(p_metadataDir));

  QSet<QString> seenNames;

  // Files: unique, safe, physically present as regular files.
  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
    QString name;
    QString fileId;
    if (!validateFileRecord(value, &name, &fileId, p_error)) {
      return false;
    }
    if (p_seenIds->contains(fileId)) {
      *p_error = QObject::tr("Duplicate id \"%1\" in %2").arg(fileId, configPath);
      return false;
    }
    p_seenIds->insert(fileId);
    if (!isSafeChildName(name)) {
      *p_error = QObject::tr("Unsafe child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    const QString folded = foldForCompare(name, p_caseSensitive);
    if (seenNames.contains(folded)) {
      *p_error =
          QObject::tr("Duplicate or colliding child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    seenNames.insert(folded);

    const QString childContent = p_contentDir + QLatin1Char('/') + name;
    if (isLinkOrReparsePoint(childContent)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(childContent);
      return false;
    }
    const QFileInfo contentInfo(childContent);
    if (!contentInfo.exists() || !contentInfo.isFile()) {
      *p_error =
          QObject::tr("Indexed file \"%1\" is missing from disk under %2").arg(name, p_contentDir);
      return false;
    }
  }

  // Folders: unique, safe, with a matching descendant config AND directory.
  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
    if (!value.isString()) {
      *p_error = QObject::tr("A folder entry in %1 is not a string.").arg(configPath);
      return false;
    }
    const QString name = value.toString();
    if (!isSafeChildName(name)) {
      *p_error = QObject::tr("Unsafe child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    const QString folded = foldForCompare(name, p_caseSensitive);
    if (seenNames.contains(folded)) {
      *p_error =
          QObject::tr("Duplicate or colliding child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    seenNames.insert(folded);

    const QString childContent = p_contentDir + QLatin1Char('/') + name;
    if (isLinkOrReparsePoint(childContent)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(childContent);
      return false;
    }
    const QFileInfo childInfo(childContent);
    if (!childInfo.exists() || !childInfo.isDir()) {
      *p_error = QObject::tr("Indexed folder \"%1\" is missing from disk under %2")
                     .arg(name, p_contentDir);
      return false;
    }

    if (!validateFolderMetadata(p_metadataDir + QLatin1Char('/') + name, childContent, name,
                                p_caseSensitive, p_visitedDirs, p_seenIds, p_error)) {
      return false;
    }
  }

  return true;
}

// Reject metadata directories that hold a vx.json but were never reached
// through the index walk (orphans), so a bundle can never carry records the
// importer would have no parent for.
bool rejectOrphanMetadata(const QString &p_metadataRoot, const QSet<QString> &p_visitedDirs,
                          QString *p_error) {
  QVector<TreeEntry> entries;
  if (!enumerateTree(p_metadataRoot, QString(), &entries, CancelFn(), p_error)) {
    return false;
  }
  for (const TreeEntry &entry : entries) {
    if (!entry.isDir) {
      continue;
    }
    const QString dir = QDir::cleanPath(p_metadataRoot + QLatin1Char('/') + entry.rel);
    const QFileInfo configInfo(dir + QLatin1Char('/') + QLatin1String(kFolderConfigFile));
    if (configInfo.exists() && !p_visitedDirs.contains(dir)) {
      *p_error = QObject::tr("Orphan folder metadata found at %1").arg(dir);
      return false;
    }
  }
  return true;
}

// Full strict validation of one (metadata, content) pair.
bool validateMetadataSubtree(const QString &p_metadataRoot, const QString &p_contentRoot,
                             const QString &p_folderName, bool p_caseSensitive, QString *p_error) {
  QSet<QString> visited;
  QSet<QString> seenIds;
  if (!validateFolderMetadata(p_metadataRoot, p_contentRoot, p_folderName, p_caseSensitive,
                              &visited, &seenIds, p_error)) {
    return false;
  }
  return rejectOrphanMetadata(p_metadataRoot, visited, p_error);
}

// ---------------------------------------------------------------------------
// Copy / hash
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

// Copy an enumerated tree, preserving empty directories, recording a SHA-256
// per file as the bytes stream through.
bool copyTree(const QString &p_srcRoot, const QString &p_dstRoot,
              const QVector<TreeEntry> &p_entries, QHash<QString, QByteArray> *p_outHashes,
              const CancelFn &p_isCancelled, const ProgressFn &p_progress, QString *p_error) {
  QDir dir;
  if (!dir.mkpath(p_dstRoot)) {
    *p_error = QObject::tr("Cannot create %1").arg(p_dstRoot);
    return false;
  }

  // Directories first (entries are produced parent-before-child by the walker,
  // but be explicit so an empty directory is never missed).
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

// Re-enumerate a source tree and require it to be byte-identical (same entry
// set, types, sizes, timestamps AND content hashes) to what was copied.
bool revalidateSource(const QString &p_root, const QVector<TreeEntry> &p_inventory,
                      const QHash<QString, QByteArray> &p_hashes, const CancelFn &p_isCancelled,
                      const ProgressFn &p_progress, QString *p_error) {
  QVector<TreeEntry> current;
  if (!enumerateTree(p_root, QString(), &current, p_isCancelled, p_error)) {
    return false;
  }

  if (current.size() != p_inventory.size()) {
    *p_error = QObject::tr("The source folder changed while it was being copied.");
    return false;
  }
  QHash<QString, TreeEntry> before;
  before.reserve(p_inventory.size());
  for (const TreeEntry &entry : p_inventory) {
    before.insert(entry.rel, entry);
  }
  for (const TreeEntry &entry : current) {
    auto it = before.constFind(entry.rel);
    if (it == before.constEnd() || !(*it == entry)) {
      *p_error =
          QObject::tr("The source folder changed while it was being copied (%1).").arg(entry.rel);
      return false;
    }
  }

  for (const TreeEntry &entry : current) {
    if (entry.isDir) {
      continue;
    }
    if (cancelled(p_isCancelled)) {
      return false;
    }
    QByteArray hash;
    if (!hashFileChunked(p_root + QLatin1Char('/') + entry.rel, &hash, p_isCancelled, p_progress,
                         p_error)) {
      return false;
    }
    if (hash != p_hashes.value(entry.rel)) {
      *p_error =
          QObject::tr("The source folder changed while it was being copied (%1).").arg(entry.rel);
      return false;
    }
  }
  return true;
}

// Verify the STAGED tree against the source inventory and the hashes recorded
// while copying. This is the only check that covers unindexed files, hidden
// files and empty directories in the staged package — validateMetadataSubtree()
// only knows about INDEXED content.
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

} // namespace

// ---------------------------------------------------------------------------
// FolderSharePackager
// ---------------------------------------------------------------------------

QString FolderSharePackager::reservedPackageDirName() { return QLatin1String(kReservedPackageDir); }

bool FolderSharePackager::destinationIsCaseSensitive(const QString &p_directory) {
  const QString probeName =
      QStringLiteral(".vnote-case-probe-") + QUuid::createUuid().toString(QUuid::Id128);
  const QString lower = p_directory + QLatin1Char('/') + probeName.toLower();
  const QString upper = p_directory + QLatin1Char('/') + probeName.toUpper();

  QFile probe(lower);
  if (!probe.open(QIODevice::WriteOnly)) {
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
  }
  probe.close();
  const bool upperExists = QFileInfo::exists(upper);
  QFile::remove(lower);
  // If the upper-cased name resolves to the file we just created, the
  // filesystem folds case.
  return !upperExists;
}

FolderSharePackager::Result FolderSharePackager::run(const Request &p_request,
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
    qCWarning(folderShareLog) << "folder share failed:" << p_message;
    return result;
  };
  auto cancel = [&result]() {
    result.m_status = Status::Cancelled;
    return result;
  };

  QString error;

  // ---- 1. Preflight -------------------------------------------------------
  setPhase(Phase::Validating);

  const bool caseSensitive = destinationIsCaseSensitive(p_request.m_destinationParent);

  // The ancestor chains are checked FIRST: vxcore proved the folder is indexed,
  // but an ancestor replaced by a junction would still have resolved. Both
  // chains are walked from the notebook root, so vx_notebook/ and contents/ are
  // covered too.
  const QString contentRelative =
      QDir(p_request.m_notebookRoot).relativeFilePath(p_request.m_contentRoot);
  const QString metadataRelative =
      QDir(p_request.m_notebookRoot).relativeFilePath(p_request.m_metadataRoot);
  if (!ancestorChainIsSafe(p_request.m_notebookRoot, contentRelative, &error) ||
      !ancestorChainIsSafe(p_request.m_notebookRoot, metadataRelative, &error)) {
    return fail(error);
  }

  QVector<TreeEntry> contentEntries;
  QVector<TreeEntry> metadataEntries;
  if (!enumerateTree(p_request.m_contentRoot, QString(), &contentEntries, isCancelled, &error)) {
    return cancelled(isCancelled) ? cancel() : fail(error);
  }
  if (!enumerateTree(p_request.m_metadataRoot, QString(), &metadataEntries, isCancelled, &error)) {
    return cancelled(isCancelled) ? cancel() : fail(error);
  }
  if (!rejectDestinationNameCollisions(contentEntries, caseSensitive, p_request.m_folderName,
                                       &error) ||
      !rejectDestinationNameCollisions(metadataEntries, caseSensitive,
                                       QLatin1String(kFolderConfigFile), &error)) {
    return fail(error);
  }
  if (!validateMetadataSubtree(p_request.m_metadataRoot, p_request.m_contentRoot,
                               p_request.m_folderName, caseSensitive, &error)) {
    return fail(error);
  }

  // ---- 2. Staged copy -----------------------------------------------------
  const qint64 copyBytes = totalFileBytes(contentEntries) + totalFileBytes(metadataEntries);
  // copy pass + source re-hash + staged re-hash.
  const qint64 totalBytes = copyBytes * 3;
  qint64 doneBytes = 0;

  QString tempRoot = p_request.m_destinationParent + QLatin1Char('/') +
                     QStringLiteral(".vnote-share-") + QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().mkpath(tempRoot)) {
    return fail(QObject::tr("Cannot create a temporary folder in the destination."));
  }
  markHidden(tempRoot);

  // Everything below must clean the temporary tree up unless the publish
  // commits, so route every exit through these helpers.
  //
  // Cleanup failure is REPORTED, never swallowed: the temp tree holds a
  // complete plaintext copy of the user's notes, so telling them a cancelled
  // share "left nothing behind" while it is still on disk would be a privacy
  // problem as well as a lie.
  bool committed = false;
  auto cleanupTemp = [&tempRoot, &committed]() -> bool {
    if (committed || tempRoot.isEmpty()) {
      return true;
    }
    if (QDir(tempRoot).removeRecursively()) {
      return true;
    }
    qCWarning(folderShareLog) << "folder share: could not remove the temporary tree" << tempRoot;
    return false;
  };
  auto leftoverMessage = [&tempRoot]() {
    return QObject::tr("A temporary copy could not be removed and is still at %1. Delete it "
                       "manually.")
        .arg(QDir::toNativeSeparators(tempRoot));
  };
  auto failAndClean = [&fail, &cleanupTemp, &leftoverMessage](const QString &p_message) {
    if (!cleanupTemp()) {
      return fail(p_message + QLatin1Char('\n') + leftoverMessage());
    }
    return fail(p_message);
  };
  auto cancelAndClean = [&cancel, &fail, &cleanupTemp, &leftoverMessage]() {
    if (!cleanupTemp()) {
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

  const QString stagedContent = tempRoot + QLatin1Char('/') + p_request.m_folderName;
  const QString stagedMetadata = tempRoot + QLatin1Char('/') + QLatin1String(kReservedPackageDir) +
                                 QStringLiteral("/contents/") + p_request.m_folderName;

  QHash<QString, QByteArray> contentHashes;
  QHash<QString, QByteArray> metadataHashes;

  if (!copyTree(p_request.m_contentRoot, stagedContent, contentEntries, &contentHashes, isCancelled,
                progress, &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }
  if (p_request.m_failureInjection == QLatin1String("copy")) {
    return failAndClean(QObject::tr("Injected copy failure."));
  }
  if (!copyTree(p_request.m_metadataRoot, stagedMetadata, metadataEntries, &metadataHashes,
                isCancelled, progress, &error)) {
    return cancelled(isCancelled) ? cancelAndClean() : failAndClean(error);
  }

  // ---- 3. Verify ----------------------------------------------------------
  //
  // ORDER MATTERS. The staged passes below report progress, and the caller's
  // progress callback pumps the event loop (it drives a modal QProgressDialog),
  // so a timer or queued callback can mutate the source WHILE they run. The
  // source checks are therefore performed LAST, with progress reporting
  // DISABLED, so no event is processed between the final observation of the
  // source and the publish.
  //
  // Four independent facts must all hold before publishing:
  //   (a) the STAGED tree is complete and its bytes match what we read;
  //   (b) the staged metadata package stands on its own;
  //   (c) neither source chain became a link/reparse point while we copied
  //       (re-enumerating the selected ROOT alone cannot see an ANCESTOR that
  //       was swapped for a junction after preflight);
  //   (d) the SOURCE is still byte-identical to what was copied.
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
  if (!validateMetadataSubtree(stagedMetadata, stagedContent, p_request.m_folderName, caseSensitive,
                               &error)) {
    return failAndClean(error);
  }

  // Announce the publish phase HERE, while a callback is still allowed to pump
  // the event loop. Everything after this point must be callback-free.
  setPhase(Phase::Publishing);
  if (p_request.m_failureInjection == QLatin1String("publish")) {
    return failAndClean(QObject::tr("Injected publish failure."));
  }

  // ---- 4. Final section: NOTHING below may pump the event loop -------------
  //
  // Pass null callbacks so the last source observation and the publish are one
  // uninterrupted step. The rename retry loop sleeps with QThread::msleep(),
  // which does NOT process events.
  if (cancelled(isCancelled)) {
    return cancelAndClean();
  }
  if (!ancestorChainIsSafe(p_request.m_notebookRoot, contentRelative, &error) ||
      !ancestorChainIsSafe(p_request.m_notebookRoot, metadataRelative, &error)) {
    return failAndClean(error);
  }
  if (!revalidateSource(p_request.m_contentRoot, contentEntries, contentHashes, CancelFn(),
                        ProgressFn(), &error)) {
    return failAndClean(error);
  }
  if (!revalidateSource(p_request.m_metadataRoot, metadataEntries, metadataHashes, CancelFn(),
                        ProgressFn(), &error)) {
    return failAndClean(error);
  }
  // The caller's own last-moment assertion (e.g. "no open note gained an
  // in-memory edit"), which the filesystem checks above cannot make.
  if (p_callbacks.m_finalPrecondition) {
    QString preconditionError;
    if (!p_callbacks.m_finalPrecondition(&preconditionError)) {
      return failAndClean(preconditionError.isEmpty()
                              ? QObject::tr("The folder changed while it was being prepared.")
                              : preconditionError);
    }
  }

  // ---- 5. Atomic publish --------------------------------------------------
  //
  // The successful rename is the COMMIT POINT. Once it succeeds the job is
  // committed and a later cancel can no longer withdraw the final directory,
  // so it must not be reported as a cancellation either.

  for (int attempt = 1; attempt <= kMaxNameAttempts; ++attempt) {
    const QString candidate = attempt == 1 ? p_request.m_folderName + QLatin1String(kBundleSuffix)
                                           : p_request.m_folderName + QLatin1String(kBundleSuffix) +
                                                 QStringLiteral(" (%1)").arg(attempt);
    const QString target = p_request.m_destinationParent + QLatin1Char('/') + candidate;
    if (QFileInfo::exists(target)) {
      continue; // Name taken: advance the suffix.
    }

    // A rename can fail transiently even though the target is FREE — on
    // Windows an on-access virus scanner or the search indexer can hold a
    // handle on the directory we just wrote. Retry the SAME name briefly.
    bool nameClaimedByOther = false;
    for (int retry = 0; retry < kRenameRetries; ++retry) {
      if (QDir().rename(tempRoot, target)) {
        committed = true;
        result.m_status = Status::Succeeded;
        result.m_bundlePath = target;
        qCInfo(folderShareLog) << "folder share published at" << target;
        return result;
      }
      if (QFileInfo::exists(target)) {
        // A destination race claimed the name between the check and the
        // rename; advance the suffix.
        nameClaimedByOther = true;
        break;
      }
      QThread::msleep(kRenameRetryDelayMs);
    }

    if (!nameClaimedByOther) {
      // The name was free the whole time and the rename still failed, so the
      // cause is NOT a collision (permissions, path length, a locked staging
      // directory, an unsupported destination). Walking a thousand suffixes
      // would repeat the same failure while blocking the GUI for minutes, and
      // could publish "Alpha-bundle (2)" for a name that was never taken.
      return failAndClean(QObject::tr("Could not move the prepared bundle into %1.")
                              .arg(QDir::toNativeSeparators(p_request.m_destinationParent)));
    }
  }

  return failAndClean(QObject::tr("Could not create the bundle folder in the destination."));
}
