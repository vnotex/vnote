#include "legacyimagemigrationcontroller.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVarLengthArray>

#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/notebookcoreservice.h>
#include <utils/fileutils2.h>
#include <vtextedit/markdownutils.h>
#include <vxcore/notebook_json_keys.h>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

using namespace vnotex;

namespace {

const QString c_legacyImageFolderVx = QStringLiteral("vx_images");
const QString c_legacyImageFolderV = QStringLiteral("_v_images");
const QString c_optOutKey = QStringLiteral("legacyImageMigrationOptOut");

#if defined(Q_OS_WIN)
// QFileInfo::canonicalFilePath() does NOT resolve NTFS junctions on Windows
// (QFileInfo::isSymLink() is false for them), so a junction inside the notebook
// pointing outside it would survive a Qt-only "canonical" compare. Resolve the
// real path through the OS instead. Returns an empty string when the path
// cannot be opened.
QString finalPathWin(const QString &p_path) {
  const QString native = QDir::toNativeSeparators(p_path);
  HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return QString();
  }

  QString result;
  DWORD needed =
      ::GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (needed > 0) {
    QVarLengthArray<wchar_t, MAX_PATH> buf(static_cast<int>(needed) + 1);
    const DWORD written = ::GetFinalPathNameByHandleW(handle, buf.data(), needed,
                                                      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written > 0 && written <= needed) {
      result = QString::fromWCharArray(buf.data(), static_cast<int>(written));
    }
  }
  ::CloseHandle(handle);

  if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) {
    result = QStringLiteral("\\\\") + result.mid(8);
  } else if (result.startsWith(QStringLiteral("\\\\?\\"))) {
    result = result.mid(4);
  }
  return QDir::fromNativeSeparators(result);
}
#endif

// CANONICAL (symlink/junction resolved) + cleaned, lower-cased on Windows so
// containment and dedup keys are case-insensitive exactly where the filesystem
// is. Resolving links is what makes the containment checks resistant to a
// directory symlink/junction inside the notebook that points outside it; a
// purely lexical compare would accept such a path and deleteAsset() would then
// delete the outside target. Falls back to the absolute path for entries that
// cannot be resolved (e.g. they do not exist).
QString normalizeForCompare(const QString &p_path) {
  if (p_path.isEmpty()) {
    return QString();
  }
  const QFileInfo info(p_path);
  QString abs;
#if defined(Q_OS_WIN)
  abs = finalPathWin(info.absoluteFilePath());
#endif
  if (abs.isEmpty()) {
    abs = info.canonicalFilePath();
  }
  if (abs.isEmpty()) {
    abs = info.absoluteFilePath();
  }
  abs = QDir::cleanPath(abs);
#if defined(Q_OS_WIN)
  abs = abs.toLower();
#endif
  return abs;
}

// Snapshot of the regular files directly inside @p_dir, by FILE NAME
// (case-folded on Windows). Used to attribute rollback to files that actually
// appeared, rather than to whatever path the inserter claims: vxcore's
// InsertAsset COPIES first and only then computes the notebook-relative path,
// so a post-copy failure returns an empty string while the copy is already on
// disk. Names rather than canonical identities keep this O(entries) with no
// per-file syscall — both snapshots come from the same concrete directory, so
// name equality is exactly the right comparison.
QStringList assetsSnapshot(const QString &p_dir) {
  if (p_dir.isEmpty()) {
    return QStringList();
  }
  QDir dir(p_dir);
  if (!dir.exists()) {
    return QStringList();
  }
  return dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
}

QString fileNameKey(const QString &p_name) {
#if defined(Q_OS_WIN)
  return p_name.toLower();
#else
  return p_name;
#endif
}

} // namespace

// True when @p_path is @p_dir itself or lives underneath it, compared
// canonically. Hand-rolled rather than PathUtils::pathContains() so the Windows
// case-folding AND the symlink resolution are explicit.
bool LegacyImageMigrationController::isPathContained(const QString &p_dir, const QString &p_path) {
  const QString dir = normalizeForCompare(p_dir);
  const QString path = normalizeForCompare(p_path);
  if (dir.isEmpty() || path.isEmpty()) {
    return false;
  }
  if (dir == path) {
    return true;
  }
  const QString prefix = dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/');
  return path.startsWith(prefix);
}

LegacyImageMigrationController::LegacyImageMigrationController(ServiceLocator &p_services,
                                                               QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

// ============ Pure helpers ============

bool LegacyImageMigrationController::isLegacyFolderName(const QString &p_name) {
  return p_name.compare(c_legacyImageFolderVx, Qt::CaseInsensitive) == 0 ||
         p_name.compare(c_legacyImageFolderV, Qt::CaseInsensitive) == 0;
}

bool LegacyImageMigrationController::containsPercentEscape(const QString &p_url) {
  static const QRegularExpression re(QStringLiteral("%[0-9A-Fa-f]{2}"));
  return re.match(p_url).hasMatch();
}

QVector<LegacyImageRef>
LegacyImageMigrationController::detect(const QString &p_markdownText, const QString &p_basePath,
                                       const QString &p_assetsFolderToExclude) {
  QVector<LegacyImageRef> results;
  if (p_markdownText.isEmpty() || p_basePath.isEmpty()) {
    return results;
  }

  // Already sorted DESCENDING by destination start; filtering preserves that.
  // Reference-style images sort last with no span and are rejected below --
  // leaving them unmigrated, which is the safe direction.
  const auto images =
      vte::MarkdownUtils::fetchImageLinks(p_markdownText, p_basePath,
                                          vte::MarkdownLink::TypeFlag::LocalRelativeInternal |
                                              vte::MarkdownLink::TypeFlag::LocalRelativeExternal);

  for (const auto &img : images) {
    // HTML `<img>` images are OUT OF SCOPE for migration: migrating one means
    // substituting the destination text in place, which is a Markdown-shaped
    // edit (see the literal-spelling guard below). Note that
    // referencedSourceKeys() deliberately does NOT filter by syntax -- it is a
    // liveness check, and an HTML reference keeps an asset alive.
    if (img.m_syntax != vte::MarkdownLink::Syntax::Markdown) {
      continue;
    }

    if (!img.m_exists || !img.hasUrlSpan() || img.m_urlInLink.isEmpty() || img.m_path.isEmpty()) {
      continue;
    }

    // Migrate only links whose source spelling is the destination literally.
    // The span is exact, but an escaped or angle-bracketed destination
    // (`vx_images/a\_b.png`, `<vx_images/a b.png>`) would have to be re-spelled
    // rather than substituted, and this feature has no business doing that.
    if (p_markdownText.mid(img.m_urlStart, img.m_urlEnd - img.m_urlStart) != img.m_urlInLink) {
      continue;
    }

    if (containsPercentEscape(img.m_urlInLink)) {
      continue;
    }

    // A query/fragment would make the link text and the resolved file disagree.
    if (img.m_urlInLink.contains(QLatin1Char('?')) || img.m_urlInLink.contains(QLatin1Char('#'))) {
      continue;
    }

    const QString cleanedUrl = QDir::cleanPath(img.m_urlInLink);
    if (cleanedUrl.isEmpty() || cleanedUrl.startsWith(QStringLiteral(".."))) {
      continue;
    }

    // Must be a LocalRelativeInternal-equivalent link: deleteAsset() joins a
    // notebook-root-relative path, and clearObsoleteImages() skips anything else.
    if (!isPathContained(p_basePath, img.m_path)) {
      continue;
    }

    // Accept iff some DIRECTORY segment is a legacy image folder. Covers
    // "vx_images/x.png", "./vx_images/x.png" and "sub/vx_images/x.png".
    const QStringList segments = cleanedUrl.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    bool legacy = false;
    for (int i = 0; i + 1 < segments.size(); ++i) {
      if (isLegacyFolderName(segments.at(i))) {
        legacy = true;
        break;
      }
    }
    if (!legacy) {
      continue;
    }

    // A notebook whose assetsFolder is itself named vx_images would otherwise
    // be flagged forever.
    if (!p_assetsFolderToExclude.isEmpty() &&
        isPathContained(p_assetsFolderToExclude, img.m_path)) {
      continue;
    }

    // The rmdir target is the matched folder, not the immediate parent:
    // "vx_images/icons/a.png" -> ".../vx_images".
    QString legacyFolder;
    QString walk = QFileInfo(img.m_path).absolutePath();
    while (!walk.isEmpty()) {
      const QString name = QFileInfo(walk).fileName();
      if (isLegacyFolderName(name)) {
        legacyFolder = QDir::cleanPath(walk);
        break;
      }
      const QString parent = QFileInfo(walk).absolutePath();
      if (parent == walk) {
        break;
      }
      walk = parent;
    }
    if (legacyFolder.isEmpty()) {
      continue;
    }

    LegacyImageRef ref;
    ref.urlInLink = img.m_urlInLink;
    ref.srcAbsolutePath = img.m_path;
    ref.urlStart = img.m_urlStart;
    ref.urlEnd = img.m_urlEnd;
    ref.legacyFolderAbsolutePath = legacyFolder;

    // An empty canonical path must never become a shared dedup key for
    // unrelated files: fall back to the cleaned absolute path.
    QString canonical = QFileInfo(img.m_path).canonicalFilePath();
    if (canonical.isEmpty()) {
      canonical = img.m_path;
    }
    ref.canonicalSrcKey = normalizeForCompare(canonical);

    results.append(ref);
  }

  return results;
}

QVector<LegacyImageRewrite> LegacyImageMigrationController::stageAssets(
    const QVector<LegacyImageRef> &p_refs, const AssetInserter &p_insert,
    const QString &p_assetsFolder, const Linkifier &p_linkify, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }

  QVector<LegacyImageRewrite> rewrites;
  if (p_refs.isEmpty()) {
    return rewrites;
  }

  if (!p_insert || !p_linkify) {
    if (p_error) {
      *p_error = tr("Internal error: image migration is not wired up.");
    }
    return rewrites;
  }

  QHash<QString, QString> keyToUrl;  // canonicalSrcKey -> new markdown URL
  QHash<QString, QString> keyToDest; // canonicalSrcKey -> absolute destination
  // Every file this call caused to appear in the assets folder, in creation
  // order. Attribution is by directory snapshot rather than by the inserter's
  // return value: vxcore's InsertAsset copies FIRST and only then computes the
  // notebook-relative path, so a post-copy failure returns an empty string
  // while the copy is already on disk. Trusting the return value would leak
  // that orphan.
  QStringList createdDestinations;
  QSet<QString> createdKeys; // File-name keys, so a file is rolled back exactly once.
  QString failure;

  const auto recordNewFiles = [&](const QStringList &p_before) {
    QSet<QString> beforeKeys;
    for (const auto &name : p_before) {
      beforeKeys.insert(fileNameKey(name));
    }
    for (const auto &name : assetsSnapshot(p_assetsFolder)) {
      const QString key = fileNameKey(name);
      if (key.isEmpty() || beforeKeys.contains(key) || createdKeys.contains(key)) {
        continue;
      }
      createdKeys.insert(key);
      createdDestinations.append(QDir(p_assetsFolder).filePath(name));
    }
  };

  for (const auto &ref : p_refs) {
    const auto it = keyToUrl.constFind(ref.canonicalSrcKey);
    if (it != keyToUrl.constEnd()) {
      LegacyImageRewrite rw;
      rw.oldUrlInLink = ref.urlInLink;
      rw.newUrlInLink = it.value();
      rw.urlStart = ref.urlStart;
      rw.urlEnd = ref.urlEnd;
      rw.srcAbsolutePath = ref.srcAbsolutePath;
      rw.destAbsolutePath = keyToDest.value(ref.canonicalSrcKey);
      rw.legacyFolderAbsolutePath = ref.legacyFolderAbsolutePath;
      rewrites.append(rw);
      continue;
    }

    // One insertAsset() per DISTINCT source file. Per-occurrence insertion
    // would produce a.png and a_1.png for a note that references one image
    // twice (vxcore appends _1, _2... on collision).
    const QStringList before = assetsSnapshot(p_assetsFolder);
    QString dest = p_insert(ref.srcAbsolutePath);
    // Register whatever appeared BEFORE inspecting the result, so the failure
    // branches below roll back a copy the inserter did not tell us about.
    recordNewFiles(before);

    if (dest.isEmpty()) {
      failure = tr("Failed to copy \"%1\" into the assets folder.").arg(ref.srcAbsolutePath);
      break;
    }

    if (QDir::isRelativePath(dest)) {
      if (p_assetsFolder.isEmpty()) {
        failure = tr("Failed to resolve the assets folder for \"%1\".").arg(ref.srcAbsolutePath);
        break;
      }
      dest = QDir(p_assetsFolder).filePath(QFileInfo(dest).fileName());
    }

    // vxcore's post-copy relative-path computation can fail (e.g. across
    // Windows volumes) and yield ".", which would otherwise promote to the
    // assets DIRECTORY rather than a file.
    const QString destFileName = QFileInfo(dest).fileName();
    if (destFileName.isEmpty() || destFileName == QStringLiteral(".") ||
        destFileName == QStringLiteral("..") || !QFileInfo(dest).isFile()) {
      failure = tr("The copy of \"%1\" could not be located in the assets folder.")
                    .arg(ref.srcAbsolutePath);
      break;
    }

    const QString newUrl = p_linkify(dest);
    if (newUrl.isEmpty()) {
      failure = tr("Failed to compute a link for \"%1\".").arg(dest);
      break;
    }

    keyToUrl.insert(ref.canonicalSrcKey, newUrl);
    keyToDest.insert(ref.canonicalSrcKey, dest);

    LegacyImageRewrite rw;
    rw.oldUrlInLink = ref.urlInLink;
    rw.newUrlInLink = newUrl;
    rw.urlStart = ref.urlStart;
    rw.urlEnd = ref.urlEnd;
    rw.srcAbsolutePath = ref.srcAbsolutePath;
    rw.destAbsolutePath = dest;
    rw.legacyFolderAbsolutePath = ref.legacyFolderAbsolutePath;
    rewrites.append(rw);
  }

  if (!failure.isEmpty()) {
    // All-or-nothing: undo every copy this call created. Nothing has been
    // rewritten in the document yet.
    for (const auto &dest : createdDestinations) {
      if (!QFile::remove(dest)) {
        qWarning() << "LegacyImageMigrationController: failed to roll back staged copy" << dest;
      }
    }
    if (p_error) {
      *p_error = failure;
    }
    return QVector<LegacyImageRewrite>();
  }

  return rewrites;
}

bool LegacyImageMigrationController::diskStateSatisfies(
    const QString &p_decodedText, const QVector<LegacyImageRewrite> &p_rewrites) {
  for (const auto &rw : p_rewrites) {
    if (!rw.newUrlInLink.isEmpty() && !p_decodedText.contains(rw.newUrlInLink)) {
      return false;
    }
    if (!rw.oldUrlInLink.isEmpty() && p_decodedText.contains(rw.oldUrlInLink)) {
      return false;
    }
  }
  return true;
}

bool LegacyImageMigrationController::finalizeGateSatisfied(
    bool p_bufferDirty, bool p_saveQueueBusy, const QString &p_decodedDiskText,
    const QVector<LegacyImageRewrite> &p_rewrites) {
  // isDirty() alone is NOT sufficient: syncNow() clears the dirty flag the
  // moment it enqueues, so a claimed worker holding an OLD snapshot could still
  // land after the check passed and the originals were gone.
  if (p_bufferDirty || p_saveQueueBusy) {
    return false;
  }
  return diskStateSatisfies(p_decodedDiskText, p_rewrites);
}

QSet<QString> LegacyImageMigrationController::referencedSourceKeys(const QString &p_decodedText,
                                                                   const QString &p_basePath) {
  QSet<QString> keys;
  if (p_decodedText.isEmpty() || p_basePath.isEmpty()) {
    return keys;
  }

  const auto add = [&keys](const QString &p_absPath) {
    if (p_absPath.isEmpty()) {
      return;
    }
    const QString key = normalizeForCompare(p_absPath);
    if (!key.isEmpty()) {
      keys.insert(key);
    }
  };

  // EVERY local shape, not just the relative ones this feature migrates: an
  // absolute path or a file: URL added after the migration is just as much a
  // live reference to the original, and deleting it would break the note.
  //
  // For the same reason there is deliberately NO Syntax filter here, even
  // though detect() skips HTML images. This runs immediately before deleting a
  // migrated original: if a note referenced one legacy asset from both a
  // Markdown link and an `<img>`, excluding HTML would let the original be
  // deleted once the Markdown reference had been rewritten, silently breaking
  // the tag.
  //
  // One call suffices now. This used to be unioned with a second, relative-only
  // resolver, because the old implementation located destinations by searching
  // the text and dropped whatever it could not find, and because it classified
  // a relative link to a missing file as Remote -- so a live reference could
  // fall out of the local set entirely and the original would be deleted.
  // Positions now come from the parser and classification is syntactic, so
  // nothing is dropped.
  const int localFlags = vte::MarkdownLink::TypeFlag::LocalRelativeInternal |
                         vte::MarkdownLink::TypeFlag::LocalRelativeExternal |
                         vte::MarkdownLink::TypeFlag::LocalAbsolute;
  for (const auto &link : vte::MarkdownUtils::fetchImageLinks(
           p_decodedText, p_basePath, static_cast<vte::MarkdownLink::TypeFlags>(localFlags))) {
    add(link.m_path);
  }

  return keys;
}

bool LegacyImageMigrationController::isStillReferenced(const QString &p_srcAbsolutePath,
                                                       const QSet<QString> &p_referencedKeys) {
  const QString key = normalizeForCompare(p_srcAbsolutePath);
  return !key.isEmpty() && p_referencedKeys.contains(key);
}

// ============ Per-notebook opt-out ============

bool LegacyImageMigrationController::isOptedOut(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return false;
  }

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return false;
  }

  const QJsonObject cfg = notebookSvc->getNotebookConfig(p_notebookId);
  if (cfg.isEmpty()) {
    // Fail open: a config we cannot read must not silently disable the feature.
    return false;
  }

  const QJsonObject metadata = cfg.value(QLatin1String(vxcore::kJsonKeyMetadata)).toObject();
  return metadata.value(c_optOutKey).toBool(false);
}

bool LegacyImageMigrationController::setOptedOut(const QString &p_notebookId) {
  if (p_notebookId.isEmpty()) {
    return false;
  }

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return false;
  }

  // Read-modify-write: vxcore does NOT support partial config updates.
  QJsonObject cfg = notebookSvc->getNotebookConfig(p_notebookId);
  if (cfg.isEmpty()) {
    return false;
  }

  QJsonObject metadata = cfg.value(QLatin1String(vxcore::kJsonKeyMetadata)).toObject();
  metadata[c_optOutKey] = true;
  cfg[QLatin1String(vxcore::kJsonKeyMetadata)] = metadata;

  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
  return notebookSvc->updateNotebookConfig(p_notebookId, cfgJson);
}

// ============ Close-time finalize ============

LegacyImageMigrationController::FinalizeResult
LegacyImageMigrationController::finalize(Buffer2 &p_buffer,
                                         const QVector<LegacyImageRewrite> &p_rewrites,
                                         bool p_bufferDirty, bool p_saveQueueBusy) {
  if (p_rewrites.isEmpty()) {
    return FinalizeResult::Done;
  }

  if (!p_buffer.isValid()) {
    return FinalizeResult::NotYet;
  }

  // Gate 3: the file ON DISK is the truth. Whatever the user's last action was
  // (including an undo of the migration), the disk state at close decides.
  const QString notePath = p_buffer.resolvedPath();
  if (notePath.isEmpty()) {
    return FinalizeResult::NotYet;
  }

  QByteArray raw;
  const Error err = FileUtils2::readFile(notePath, &raw);
  if (err) {
    qWarning() << "LegacyImageMigrationController: cannot read note for finalize:" << notePath;
    return FinalizeResult::NotYet;
  }

  const QString diskText = p_buffer.decode(raw);
  if (!finalizeGateSatisfied(p_bufferDirty, p_saveQueueBusy, diskText, p_rewrites)) {
    return FinalizeResult::NotYet;
  }

  // The gate above compares URL SPELLINGS, which cannot see a link the user
  // added after the migration that resolves to the same original through a
  // different spelling (e.g. "vx_images/./a.png"). Resolve the final on-disk
  // text and refuse to delete anything it still points at.
  const QSet<QString> stillReferenced = referencedSourceKeys(diskText, QFileInfo(notePath).path());

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return FinalizeResult::NotYet;
  }

  const QString notebookRoot =
      notebookSvc->buildAbsolutePath(p_buffer.nodeId().notebookId, QString());
  if (notebookRoot.isEmpty()) {
    return FinalizeResult::NotYet;
  }

  int attempted = 0;
  int failed = 0;
  QSet<QString> deletedKeys;
  QSet<QString> legacyFolders;

  for (const auto &rw : p_rewrites) {
    if (!rw.legacyFolderAbsolutePath.isEmpty()) {
      legacyFolders.insert(rw.legacyFolderAbsolutePath);
    }

    if (rw.srcAbsolutePath.isEmpty()) {
      continue;
    }
    const QString key = normalizeForCompare(rw.srcAbsolutePath);
    if (key.isEmpty() || deletedKeys.contains(key)) {
      continue;
    }
    deletedKeys.insert(key);

    // A link added after the migration may resolve to this original through a
    // spelling the URL comparison above cannot see. Keeping the file is the
    // safe direction (a copy rather than a move).
    if (isStillReferenced(rw.srcAbsolutePath, stillReferenced)) {
      qWarning() << "LegacyImageMigrationController: original is still referenced, keeping"
                 << rw.srcAbsolutePath;
      continue;
    }

    // deleteAsset() joins a notebook-root-relative path onto the notebook root
    // WITHOUT decoding, so derive it from the resolved absolute path rather
    // than from the URL spelling, and refuse anything outside the root. The
    // containment test is CANONICAL, so a directory junction inside the
    // notebook cannot be used to reach a file outside it.
    if (!isPathContained(notebookRoot, rw.srcAbsolutePath)) {
      qWarning() << "LegacyImageMigrationController: skipping out-of-notebook original"
                 << rw.srcAbsolutePath;
      continue;
    }

    const QString relPath = QDir::cleanPath(
        QDir(notebookRoot).relativeFilePath(QFileInfo(rw.srcAbsolutePath).absoluteFilePath()));
    if (relPath.isEmpty() || relPath.startsWith(QStringLiteral(".."))) {
      qWarning() << "LegacyImageMigrationController: refusing suspicious asset path" << relPath;
      continue;
    }

    ++attempted;
    if (!p_buffer.deleteAsset(relPath)) {
      ++failed;
      qWarning() << "LegacyImageMigrationController: failed to delete legacy image" << relPath;
    }
  }

  // A VNote3 vx_images/ usually still holds vx.json, so it legitimately
  // survives. Do NOT special-case vx.json.
  for (const auto &folder : legacyFolders) {
    QDir dir(folder);
    if (dir.exists() && dir.isEmpty()) {
      if (!QDir().rmdir(folder)) {
        qWarning() << "LegacyImageMigrationController: failed to remove empty legacy folder"
                   << folder;
      }
    }
  }

  if (attempted > 0 && failed == attempted) {
    return FinalizeResult::Failed;
  }
  return FinalizeResult::Done;
}
