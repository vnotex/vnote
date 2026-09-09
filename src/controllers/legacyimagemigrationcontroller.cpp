#include "legacyimagemigrationcontroller.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMap>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextCodec>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVarLengthArray>

#include <algorithm>

#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <utils/fileutils2.h>
#include <vtextedit/markdownutils.h>
#include <vxcore/notebook_json_keys.h>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

using namespace vnotex;

namespace {

Q_LOGGING_CATEGORY(lcNoteEncryption, "vnote.encryption", QtInfoMsg)

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

// Conversion refuses reparse points at EVERY component, including an assets
// root that does not exist yet. The legacy path keeps its historical policy.
bool encryptionPathIsDirect(const QString &p_path) {
  QString path = QFileInfo(p_path).absoluteFilePath();
  while (!path.isEmpty()) {
    const QFileInfo info(path);
    if (info.isSymLink()) {
      return false;
    }
#if defined(Q_OS_WIN)
    const QString native = QDir::toNativeSeparators(path);
    const DWORD attributes =
        ::GetFileAttributesW(reinterpret_cast<const wchar_t *>(native.utf16()));
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      return false;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES && ::GetLastError() != ERROR_FILE_NOT_FOUND &&
        ::GetLastError() != ERROR_PATH_NOT_FOUND) {
      return false;
    }
#endif
    const QString parent = info.absolutePath();
    if (parent == path) {
      break;
    }
    path = parent;
  }
  return true;
}

bool encryptionSourceHasOneLink(const QString &p_path) {
#if defined(Q_OS_WIN)
  const QString native = QDir::toNativeSeparators(p_path);
  HANDLE file = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  BY_HANDLE_FILE_INFORMATION info;
  const bool one = ::GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1;
  ::CloseHandle(file);
  return one;
#else
  struct stat info;
  return ::stat(QFile::encodeName(p_path).constData(), &info) == 0 && info.st_nlink == 1;
#endif
}

bool hashEncryptionSource(const QString &p_path, QByteArray &p_hash) {
  QFile file(p_path);
  if (!QFileInfo(p_path).isFile() || !encryptionPathIsDirect(p_path) ||
      !file.open(QIODevice::ReadOnly)) {
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file) || file.error() != QFileDevice::NoError) {
    return false;
  }
  p_hash = hash.result().toHex();
  return true;
}

struct EncryptionText {
  QTextCodec *codec = nullptr;
  QByteArray bom;
  QString text;

  bool decode(const QByteArray &p_bytes, const QString &p_encoding) {
    codec =
        QTextCodec::codecForName(p_encoding.isEmpty() ? QByteArray("UTF-8") : p_encoding.toUtf8());
    if (!codec) {
      return false;
    }
    const QByteArray name = codec->name().toUpper();
    QByteArray bytes = p_bytes;
    // Explicit endian codecs keep a BOM and every original CR/LF byte intact.
    // Never use QTextStream's newline translation or a lossy fallback codec.
    if (bytes.startsWith(QByteArray::fromHex("efbbbf")) && name == "UTF-8") {
      bom = bytes.left(3);
    } else if ((name == "UTF-32" || name == "UTF-32LE") &&
               bytes.startsWith(QByteArray::fromHex("fffe0000"))) {
      bom = bytes.left(4);
      codec = QTextCodec::codecForName("UTF-32LE");
    } else if ((name == "UTF-32" || name == "UTF-32BE") &&
               bytes.startsWith(QByteArray::fromHex("0000feff"))) {
      bom = bytes.left(4);
      codec = QTextCodec::codecForName("UTF-32BE");
    } else if ((name == "UTF-16" || name == "UTF-16LE") &&
               bytes.startsWith(QByteArray::fromHex("fffe"))) {
      bom = bytes.left(2);
      codec = QTextCodec::codecForName("UTF-16LE");
    } else if ((name == "UTF-16" || name == "UTF-16BE") &&
               bytes.startsWith(QByteArray::fromHex("feff"))) {
      bom = bytes.left(2);
      codec = QTextCodec::codecForName("UTF-16BE");
    }
    bytes.remove(0, bom.size());
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    text = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    QByteArray roundTrip;
    return state.invalidChars == 0 && state.remainingChars == 0 && !text.contains(QChar::Null) &&
           encode(text, roundTrip) && roundTrip == p_bytes;
  }

  bool encode(const QString &p_text, QByteArray &p_bytes) const {
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    p_bytes = bom + codec->fromUnicode(p_text.constData(), p_text.size(), &state);
    return state.invalidChars == 0 && state.remainingChars == 0;
  }
};

void fingerprintPart(QCryptographicHash &p_hash, const QByteArray &p_bytes) {
  const QByteArray size = QByteArray::number(p_bytes.size()) + ':';
  p_hash.addData(size);
  p_hash.addData(p_bytes);
}

bool encryptedCandidate(const QJsonObject &p_file, const QString &p_path) {
  return p_path.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive) ||
         p_file.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
         p_file.value(QLatin1String(vxcore::kJsonKeyMetadata))
             .toObject()
             .value(QLatin1String(vxcore::kJsonKeyEncrypted))
             .toBool();
}

bool simpleSourceName(const QString &p_name) {
  return !p_name.isEmpty() && p_name != QLatin1String(".") && p_name != QLatin1String("..") &&
         !p_name.contains(QLatin1Char('/')) && !p_name.contains(QLatin1Char('\\')) &&
         !p_name.contains(QLatin1Char(':')) && !p_name.contains(QChar::Null);
}

QString encryptionAttachmentPath(const QString &p_root, const QString &p_owned,
                                 const QJsonValue &p_value) {
  if (!p_value.isString())
    return QString();
  const QString relative = QDir::cleanPath(QDir::fromNativeSeparators(p_value.toString()));
  if (QDir::isAbsolutePath(relative) || relative == QLatin1String("..") ||
      relative.startsWith(QLatin1String("../")) || relative.contains(QChar::Null) ||
      !simpleSourceName(QFileInfo(relative).fileName()))
    return QString();
  return QDir(relative.contains(QLatin1Char('/')) ? p_root : p_owned).filePath(relative);
}

QString markdownQuotedTitle(QString p_title) {
  p_title.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  p_title.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  p_title.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
  p_title.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
  p_title.replace(QLatin1Char('\r'), QStringLiteral("&#13;"));
  p_title.replace(QLatin1Char('\n'), QStringLiteral("&#10;"));
  return p_title;
}

} // namespace

NoteEncryptionPlan
LegacyImageMigrationController::planNoteEncryption(const NodeIdentifier &p_nodeId,
                                                   const QByteArray &p_currentBody,
                                                   const QString &p_encoding) const {
  NoteEncryptionPlan plan;
  const auto fail = [&](VxCoreError p_error, const QString &p_message) {
    qCWarning(lcNoteEncryption).noquote().nospace()
        << "phase=plan_failed notebook_id="
        << QUuid(p_nodeId.notebookId).toString(QUuid::WithoutBraces) << " error=" << int(p_error);
    plan.m_error = p_error;
    plan.m_errorMessage = p_message;
    plan.m_body.clear();
    plan.m_resourcePlan = QJsonObject();
    plan.m_retainedOriginals.clear();
    plan.m_referenceFingerprint.clear();
    return plan;
  };
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *buffers = m_services.get<BufferService>();
  auto *fileTypes = m_services.get<FileTypeCoreService>();
  if (!notebooks || !buffers || !fileTypes || !p_nodeId.isValid() || p_nodeId.isRoot() ||
      p_nodeId.isVirtual() || QThread::currentThread() != buffers->asQObject()->thread()) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The note cannot be prepared for encryption"));
  }
  QMap<QString, QJsonObject> notebookRecords;
  for (const auto &entry : notebooks->listNotebooks()) {
    const auto record = entry.toObject();
    notebookRecords.insert(record.value(QLatin1String(vxcore::kJsonKeyId)).toString(), record);
  }
  if (notebookRecords.value(p_nodeId.notebookId)
          .value(QLatin1String(vxcore::kJsonKeyType))
          .toString() != QLatin1String("bundled")) {
    return fail(VXCORE_ERR_UNSUPPORTED, tr("Only managed notes can be encrypted"));
  }
  if (notebooks->isNotebookReadOnly(p_nodeId.notebookId)) {
    return fail(VXCORE_ERR_READ_ONLY, tr("The notebook is read-only"));
  }
  VxCoreError error = VXCORE_OK;
  const auto fileInfo = notebooks->getFileInfo(p_nodeId.notebookId, p_nodeId.relativePath, &error);
  if (error != VXCORE_OK || fileInfo.isEmpty()) {
    return fail(error == VXCORE_OK ? VXCORE_ERR_NOT_FOUND : error, tr("The note no longer exists"));
  }
  if (encryptedCandidate(fileInfo, p_nodeId.relativePath)) {
    return fail(VXCORE_ERR_UNSUPPORTED, tr("The note is already protected"));
  }
  const QString editorType = fileTypes->getFileType(p_nodeId.relativePath).m_typeName.toLower();
  if (editorType != QLatin1String("markdown") && editorType != QLatin1String("text")) {
    return fail(VXCORE_ERR_UNSUPPORTED, tr("Only Markdown and text notes can be encrypted"));
  }
  const QUuid fileId(fileInfo.value(QLatin1String(vxcore::kJsonKeyId)).toString());
  const QString root = notebooks->buildAbsolutePath(p_nodeId.notebookId, QString());
  const QString source = notebooks->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
  const QString owned = notebooks->getAttachmentsFolder(p_nodeId.notebookId, p_nodeId.relativePath);
  if (fileId.isNull() || root.isEmpty() || source.isEmpty() || owned.isEmpty() ||
      !isPathContained(root, source) || !encryptionPathIsDirect(source)) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The note has an unverifiable storage location"));
  }
  if (!isPathContained(root, owned)) {
    return fail(VXCORE_ERR_UNSUPPORTED,
                tr("Encryption requires an assets folder inside the notebook"));
  }
  if (!encryptionPathIsDirect(owned)) {
    return fail(VXCORE_ERR_UNSUPPORTED, tr("Encryption cannot follow symbolic links or junctions"));
  }
  qCInfo(lcNoteEncryption).noquote().nospace()
      << "phase=plan_begin notebook_id="
      << QUuid(p_nodeId.notebookId).toString(QUuid::WithoutBraces)
      << " note_id=" << fileId.toString(QUuid::WithoutBraces);
  QByteArray sourceHash;
  if (!hashEncryptionSource(source, sourceHash)) {
    return fail(VXCORE_ERR_IO, tr("The note could not be read"));
  }
  EncryptionText selectedText;
  if (!selectedText.decode(p_currentBody, p_encoding)) {
    return fail(VXCORE_ERR_UNSUPPORTED,
                tr("The note cannot be rewritten losslessly using the selected encoding"));
  }
  if (p_currentBody.startsWith(QByteArray("VNOTEE1\0", 8))) {
    return fail(VXCORE_ERR_ENCRYPTION_FORMAT, tr("The note has inconsistent encryption metadata"));
  }

  struct KnownNote {
    NodeIdentifier id;
    QString path;
    QJsonObject metadata;
    QString editorType;
    bool encrypted = false;
  };
  QMap<QString, KnownNote> knownNotes;
  QCryptographicHash checkpoint(QCryptographicHash::Sha256);
  bool retainAll = false;
  const auto retainUnverified = [&](const char *p_reason, const QString &p_subjectId) {
    retainAll = true;
    qCInfo(lcNoteEncryption).noquote().nospace()
        << "phase=reference_scan_incomplete note_id=" << fileId.toString(QUuid::WithoutBraces)
        << " reason=" << p_reason
        << " subject_id=" << QUuid(p_subjectId).toString(QUuid::WithoutBraces);
  };
  // Enumerate the already-open notebook registry, not arbitrary filesystem
  // trees. Protected entries contribute visible identity only, never content.
  for (auto notebook = notebookRecords.constBegin(); notebook != notebookRecords.constEnd();
       ++notebook) {
    const QString notebookId = notebook.key();
    fingerprintPart(checkpoint, QJsonDocument(notebook.value()).toJson(QJsonDocument::Compact));
    fingerprintPart(
        checkpoint,
        QJsonDocument(notebooks->getNotebookConfig(notebookId)).toJson(QJsonDocument::Compact));
    QStringList folders{QString()};
    QSet<QString> visited;
    while (!folders.isEmpty()) {
      const QString folder = folders.takeLast();
      const QString absolute = notebooks->buildAbsolutePath(notebookId, folder);
      const QString key = normalizeForCompare(absolute);
      if (absolute.isEmpty() || !encryptionPathIsDirect(absolute) || visited.contains(key)) {
        if (notebookId == p_nodeId.notebookId) {
          return fail(VXCORE_ERR_UNSUPPORTED, tr("The notebook index cannot be verified safely"));
        }
        retainUnverified("notebook_path_unverifiable", notebookId);
        continue;
      }
      visited.insert(key);
      const auto children = notebooks->listFolderChildren(notebookId, folder, &error);
      if (error != VXCORE_OK || !children.value(QLatin1String(vxcore::kJsonKeyFiles)).isArray() ||
          !children.value(QLatin1String(vxcore::kJsonKeyFolders)).isArray()) {
        if (notebookId == p_nodeId.notebookId) {
          return fail(error == VXCORE_OK ? VXCORE_ERR_INVALID_STATE : error,
                      tr("The notebook index could not be read"));
        }
        retainUnverified("notebook_index_unreadable", notebookId);
        continue;
      }
      fingerprintPart(checkpoint, notebookId.toUtf8());
      fingerprintPart(checkpoint, folder.toUtf8());
      fingerprintPart(checkpoint, QJsonDocument(children).toJson(QJsonDocument::Compact));
      for (const auto &entry : children.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
        const auto record = entry.toObject();
        const QString name = record.value(QLatin1String(vxcore::kJsonKeyName)).toString();
        if (!simpleSourceName(name)) {
          return fail(VXCORE_ERR_INVALID_STATE,
                      tr("The notebook contains an invalid indexed name"));
        }
        KnownNote note;
        note.id =
            NodeIdentifier{notebookId, folder.isEmpty() ? name : folder + QLatin1Char('/') + name};
        note.path = notebooks->buildAbsolutePath(notebookId, note.id.relativePath);
        note.metadata = record;
        note.encrypted = encryptedCandidate(record, note.id.relativePath);
        if (!note.encrypted) {
          note.editorType = fileTypes->getFileType(name).m_typeName.toLower();
        }
        knownNotes.insert(normalizeForCompare(note.path), note);
      }
      QStringList subfolders;
      for (const auto &entry : children.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
        const QString name = entry.toObject().value(QLatin1String(vxcore::kJsonKeyName)).toString();
        if (!simpleSourceName(name)) {
          return fail(VXCORE_ERR_INVALID_STATE,
                      tr("The notebook contains an invalid indexed name"));
        }
        subfolders.append(folder.isEmpty() ? name : folder + QLatin1Char('/') + name);
      }
      std::sort(subfolders.begin(), subfolders.end());
      folders.append(subfolders);
    }
  }

  struct Resource {
    QString path;
    QString id;
    QString name;
    QString mediaType;
    QString role;
    QByteArray hash;
    bool insideOwned = false;
    bool singleLink = false;
    bool retain = true;
  };
  QMap<QString, Resource> resources;
  QMimeDatabase mimeDatabase;
  const auto addResource = [&](const QString &p_path, const QString &p_role,
                               const QString &p_name) {
    const QFileInfo info(p_path);
    if (!info.isFile() || !info.isReadable() || !encryptionPathIsDirect(p_path)) {
      return false;
    }
    const QString key = normalizeForCompare(p_path);
    if (key == normalizeForCompare(source)) {
      return false;
    }
    auto existing = resources.find(key);
    if (existing != resources.end()) {
      if (p_role == QLatin1String("comments") || existing->role == QLatin1String("comments")) {
        existing->role = QStringLiteral("comments");
        existing->mediaType = QStringLiteral("application/json");
        return true;
      }
      if (p_role == QLatin1String("image")) {
        existing->role = p_role;
      }
      return true;
    }
    Resource resource;
    resource.path = info.canonicalFilePath();
#if defined(Q_OS_WIN)
    resource.path = finalPathWin(p_path);
#endif
    if (resource.path.isEmpty() || !hashEncryptionSource(resource.path, resource.hash)) {
      return false;
    }
    resource.id = QUuid::createUuidV5(fileId, key.toUtf8()).toString(QUuid::WithoutBraces);
    resource.name = p_name.isEmpty() ? info.fileName() : p_name;
    if (resource.name.isEmpty() || resource.name == QLatin1String(".") ||
        resource.name == QLatin1String("..") ||
        std::any_of(resource.name.cbegin(), resource.name.cend(), [](QChar p_char) {
          return p_char.unicode() < 32 || p_char.unicode() == 127 || p_char == QLatin1Char('/') ||
                 p_char == QLatin1Char('\\');
        })) {
      return false;
    }
    resource.role = p_role;
    resource.mediaType =
        p_role == QLatin1String("comments")
            ? QStringLiteral("application/json")
            : mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension).name();
    resource.insideOwned = isPathContained(owned, p_path);
    resource.singleLink = resource.insideOwned && encryptionSourceHasOneLink(p_path);
    resource.retain = !resource.insideOwned || !resource.singleLink;
    resources.insert(key, resource);
    return true;
  };
  const QString commentsPath = QDir(owned).filePath(QStringLiteral("comments.json"));
  QStringList assetFolders;
  if (QFileInfo::exists(owned)) {
    if (!QFileInfo(owned).isDir() || !QFileInfo(owned).isReadable()) {
      return fail(VXCORE_ERR_IO, tr("The note's assets folder could not be read"));
    }
    assetFolders.append(owned);
  }
  while (!assetFolders.isEmpty()) {
    const QString folder = assetFolders.takeLast();
    const auto entries = QDir(folder).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const auto &entry : entries) {
      if (!encryptionPathIsDirect(entry.absoluteFilePath()) || !entry.isReadable()) {
        return fail(VXCORE_ERR_UNSUPPORTED,
                    tr("An owned resource is unreadable or traverses a symbolic link or junction"));
      }
      if (entry.isDir()) {
        assetFolders.append(entry.absoluteFilePath());
      } else {
        const bool comments =
            normalizeForCompare(entry.absoluteFilePath()) == normalizeForCompare(commentsPath);
        if (!addResource(entry.absoluteFilePath(),
                         comments ? QStringLiteral("comments") : QStringLiteral("attachment"),
                         entry.fileName())) {
          return fail(VXCORE_ERR_IO, tr("An owned resource could not be read"));
        }
      }
    }
  }
  const auto attachmentsValue = fileInfo.value(QLatin1String(vxcore::kJsonKeyAttachments));
  if (!attachmentsValue.isUndefined() && !attachmentsValue.isArray()) {
    return fail(VXCORE_ERR_INVALID_STATE, tr("The attachment list is malformed"));
  }
  for (const auto &entry : attachmentsValue.toArray()) {
    // FileRecord metadata carries notebook-relative paths; attachment-list
    // consumers may supply a basename relative to this note's owned folder.
    const QString attachmentPath = encryptionAttachmentPath(root, owned, entry);
    const QString name = QFileInfo(attachmentPath).fileName();
    if (attachmentPath.isEmpty() ||
        !addResource(attachmentPath, QStringLiteral("attachment"), name)) {
      return fail(VXCORE_ERR_IO, tr("An attachment is missing or cannot be verified"));
    }
  }

  const auto allLinkFlags =
      vte::MarkdownLink::LocalRelativeInternal | vte::MarkdownLink::LocalRelativeExternal |
      vte::MarkdownLink::LocalAbsolute | vte::MarkdownLink::QtResource | vte::MarkdownLink::Remote;
  struct Rewrite {
    int start;
    int end;
    QString text;
  };
  QVector<Rewrite> rewrites;
  QVector<vte::MarkdownLink> expectedLinks;
  if (editorType == QLatin1String("markdown")) {
    expectedLinks = vte::MarkdownUtils::fetchResourceLinks(
        selectedText.text, QFileInfo(source).absolutePath(), allLinkFlags);
    for (auto &link : expectedLinks) {
      if (!link.m_rewriteSupported) {
        return fail(VXCORE_ERR_UNSUPPORTED,
                    tr("A resource construct cannot be verified; simplify it before encrypting"));
      }
      const QUrl url(link.m_urlInLink, QUrl::TolerantMode);
      if (link.m_type & vte::MarkdownLink::Remote) {
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
            scheme == QLatin1String("data") ||
            (!link.m_isImage &&
             (scheme == QLatin1String("mailto") || scheme == QLatin1String("vx") ||
              scheme == QLatin1String("vnote"))) ||
            link.m_urlInLink.startsWith(QLatin1String("//"))) {
          continue;
        }
        return fail(VXCORE_ERR_UNSUPPORTED, tr("A resource uses an unsupported URL scheme"));
      }
      if (link.m_type & vte::MarkdownLink::QtResource) {
        return fail(VXCORE_ERR_UNSUPPORTED,
                    tr("An application resource cannot be imported as a note asset"));
      }
      const QString key = normalizeForCompare(link.m_path);
      if (!link.m_isImage && knownNotes.contains(key)) {
        continue; // Indexed-file links are navigation, not private attachments.
      }
      if (url.hasQuery() || url.hasFragment() || link.m_path.isEmpty() ||
          !addResource(link.m_path,
                       link.m_isImage ? QStringLiteral("image") : QStringLiteral("attachment"),
                       QString())) {
        return fail(VXCORE_ERR_IO, tr("A linked resource is missing, unreadable, or unverifiable"));
      }
      const QString logical = QStringLiteral("vxasset:") + resources.value(key).id;
      if (link.hasUrlSpan()) {
        rewrites.append(Rewrite{link.m_urlStart, link.m_urlEnd, logical});
      } else if (link.m_labelEnd >= 0 && link.m_regionEnd >= link.m_labelEnd) {
        QString suffix = QLatin1Char('(') + logical;
        if (!link.m_title.isEmpty()) {
          suffix += QStringLiteral(" \"") + markdownQuotedTitle(link.m_title) + QLatin1Char('"');
        }
        if (link.m_width > 0 || link.m_height > 0) {
          suffix += QStringLiteral(" =%1x%2").arg(link.m_width).arg(link.m_height);
        }
        suffix += QLatin1Char(')');
        rewrites.append(Rewrite{link.m_labelEnd, link.m_regionEnd, suffix});
      } else {
        return fail(VXCORE_ERR_UNSUPPORTED, tr("A linked resource has no safe rewrite range"));
      }
      link.m_urlInLink = logical;
    }
  }

  QSet<QString> shared;
  const auto scanReferences = [&](const QString &p_text, const QString &p_basePath,
                                  const QString &p_subjectId) {
    bool reportedUnsupported = false;
    for (const auto &link :
         vte::MarkdownUtils::fetchResourceLinks(p_text, p_basePath, allLinkFlags)) {
      if (!link.m_rewriteSupported && !reportedUnsupported) {
        retainUnverified("unsupported_reference", p_subjectId);
        reportedUnsupported = true;
      }
      if (!(link.m_type & (vte::MarkdownLink::Remote | vte::MarkdownLink::QtResource)) &&
          !link.m_path.isEmpty()) {
        shared.insert(normalizeForCompare(link.m_path));
      }
    }
  };
  QMap<QString, QJsonObject> liveBuffers;
  for (const auto &entry : buffers->listBuffers()) {
    const auto info = entry.toObject();
    const QString path = info.value(QStringLiteral("filePath")).toString();
    if (encryptedCandidate(info, path) || path.startsWith(QLatin1String("vx://"))) {
      continue;
    }
    const QString notebookId = info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    const QString absolute =
        notebookId.isEmpty() ? path : notebooks->buildAbsolutePath(notebookId, path);
    liveBuffers.insert(normalizeForCompare(absolute), info);
  }
  for (auto note = knownNotes.constBegin(); note != knownNotes.constEnd(); ++note) {
    if (note->id == p_nodeId || note->encrypted) {
      continue;
    }
    const QString subjectId = note->metadata.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    // Metadata attachment references matter even for non-Markdown viewers.
    const auto attachments = note->metadata.value(QLatin1String(vxcore::kJsonKeyAttachments));
    if (!attachments.isUndefined() && !attachments.isArray()) {
      retainUnverified("attachment_metadata_invalid", subjectId);
    }
    if (!attachments.toArray().isEmpty()) {
      const QString assets =
          notebooks->getAttachmentsFolder(note->id.notebookId, note->id.relativePath);
      const QString notebookRoot = notebooks->buildAbsolutePath(note->id.notebookId, QString());
      if (assets.isEmpty() || notebookRoot.isEmpty()) {
        retainUnverified("attachment_location_unavailable", subjectId);
      } else {
        for (const auto &attachment : attachments.toArray()) {
          const QString attachmentPath = encryptionAttachmentPath(notebookRoot, assets, attachment);
          if (attachmentPath.isEmpty()) {
            retainUnverified("attachment_path_unverifiable", subjectId);
          } else {
            shared.insert(normalizeForCompare(attachmentPath));
          }
        }
      }
    }
    if (note->editorType != QLatin1String("markdown")) {
      continue;
    }
    QByteArray bytes;
    QFile disk(note->path);
    if (!encryptionPathIsDirect(note->path) || !disk.open(QIODevice::ReadOnly)) {
      retainUnverified("note_unreadable_or_indirect", subjectId);
    } else {
      bytes = disk.readAll();
      if (disk.error() != QFileDevice::NoError || bytes.startsWith(QByteArray("VNOTEE1\0", 8))) {
        retainUnverified("note_content_unverifiable", subjectId);
      } else {
        fingerprintPart(checkpoint, note.key().toUtf8());
        fingerprintPart(checkpoint, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        const QString bufferId =
            liveBuffers.value(note.key()).value(QLatin1String(vxcore::kJsonKeyId)).toString();
        EncryptionText text;
        if (!text.decode(bytes, bufferId.isEmpty() ? QStringLiteral("UTF-8")
                                                   : buffers->bufferEncoding(bufferId))) {
          retainUnverified("note_encoding_unverifiable", subjectId);
        } else {
          scanReferences(text.text, QFileInfo(note->path).absolutePath(), subjectId);
        }
      }
    }
  }
  // Include active editor snapshots (also external/unindexed live notes), not
  // just core-buffer bytes that may lag an AutoSave/BackupFile/None writer.
  for (auto live = liveBuffers.constBegin(); live != liveBuffers.constEnd(); ++live) {
    if (live.key() == normalizeForCompare(source)) {
      continue;
    }
    const QString bufferId = live->value(QLatin1String(vxcore::kJsonKeyId)).toString();
    const QString path = live->value(QStringLiteral("filePath")).toString();
    if (fileTypes->getFileType(path).m_typeName.compare(QLatin1String("Markdown"),
                                                        Qt::CaseInsensitive) != 0) {
      continue;
    }
    fingerprintPart(checkpoint, QJsonDocument(live.value()).toJson(QJsonDocument::Compact));
    fingerprintPart(checkpoint, QByteArray::number(buffers->currentRevision(bufferId)));
    // A queued older snapshot can still introduce a reference absent from
    // both disk and the newest writer. Do not guess at queue internals.
    if (buffers->isSaveQueueBusy(bufferId)) {
      retainUnverified("pending_save", bufferId);
    }
    QString snapshot;
    if (buffers->captureActiveWriterContent(bufferId, &snapshot)) {
      fingerprintPart(checkpoint,
                      QCryptographicHash::hash(snapshot.toUtf8(), QCryptographicHash::Sha256));
      scanReferences(snapshot, QFileInfo(live.key()).absolutePath(), bufferId);
    } else if (buffers->isDirty(bufferId) || buffers->isSaveQueueBusy(bufferId)) {
      retainUnverified("dirty_note_without_snapshot", bufferId);
    } else {
      VxCoreError readError = VXCORE_OK;
      const auto buffer = buffers->getBufferHandle(bufferId);
      const QByteArray bytes = buffer.isValid() && !buffer.isEncrypted()
                                   ? buffer.getContentRaw(&readError)
                                   : QByteArray();
      EncryptionText text;
      if (!buffer.isValid() || buffer.isEncrypted() || readError != VXCORE_OK ||
          !text.decode(bytes, buffers->bufferEncoding(bufferId))) {
        retainUnverified("live_note_content_unverifiable", bufferId);
      } else {
        fingerprintPart(checkpoint, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        scanReferences(text.text, QFileInfo(live.key()).absolutePath(), bufferId);
      }
    }
  }
  fingerprintPart(checkpoint, retainAll ? QByteArray("retain") : QByteArray("exclusive"));
  plan.m_referenceFingerprint = checkpoint.result();
  QJsonArray resourceJson;
  QSet<QString> reservedNames;
  QSet<QString> attachmentNames;
  for (const auto &resource : resources) {
    if (resource.role == QLatin1String("attachment")) {
      reservedNames.insert(resource.name);
    }
  }
  int ownedCount = 0;
  int outsideCount = 0;
  int linkUnverifiedCount = 0;
  int sharedCount = 0;
  int uncertainCount = 0;
  int resourceIndex = 0;
  for (auto resource = resources.begin(); resource != resources.end(); ++resource) {
    if (resource->role == QLatin1String("attachment")) {
      if (attachmentNames.contains(resource->name)) {
        const QFileInfo info(resource->name);
        const QString suffix =
            info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
        const QString base = info.completeBaseName();
        int index = 1;
        QString name;
        do {
          // Same basename_N.ext convention as FileUtils2, but against the
          // in-memory manifest: planning never creates a file to reserve it.
          name = QStringLiteral("%1_%2%3").arg(base, QString::number(index++), suffix);
        } while (reservedNames.contains(name) || attachmentNames.contains(name));
        resource->name = name;
      }
      attachmentNames.insert(resource->name);
    }
    const bool referencedElsewhere = shared.contains(resource.key());
    ownedCount += resource->insideOwned ? 1 : 0;
    outsideCount += resource->insideOwned ? 0 : 1;
    linkUnverifiedCount += resource->insideOwned && !resource->singleLink ? 1 : 0;
    sharedCount += referencedElsewhere ? 1 : 0;
    uncertainCount += !resource->retain && !referencedElsewhere && retainAll ? 1 : 0;
    resource->retain = resource->retain || retainAll || referencedElsewhere;
    qCDebug(lcNoteEncryption).noquote().nospace()
        << "phase=resource_decision note_id=" << fileId.toString(QUuid::WithoutBraces)
        << " resource_index=" << resourceIndex++ << " role=" << resource->role
        << " inside_owned_assets=" << resource->insideOwned
        << " single_link_checked=" << resource->insideOwned
        << " single_link=" << resource->singleLink
        << " referenced_elsewhere=" << referencedElsewhere
        << " reference_scan_complete=" << !retainAll << " retain_original=" << resource->retain;
    if (resource->retain) {
      plan.m_retainedOriginals.append(resource->path);
    }
    QJsonObject object;
    object.insert(QLatin1String(vxcore::kJsonKeyResourceId), resource->id);
    object.insert(QLatin1String(vxcore::kJsonKeySourcePath), resource->path);
    object.insert(QLatin1String(vxcore::kJsonKeySourceSha256), QString::fromLatin1(resource->hash));
    object.insert(QLatin1String(vxcore::kJsonKeyName), resource->name);
    object.insert(QLatin1String(vxcore::kJsonKeyMediaType), resource->mediaType);
    object.insert(QLatin1String(vxcore::kJsonKeyRole), resource->role);
    object.insert(QLatin1String(vxcore::kJsonKeyRetainOriginal), resource->retain);
    resourceJson.append(object);
  }
  std::sort(rewrites.begin(), rewrites.end(),
            [](const Rewrite &p_a, const Rewrite &p_b) { return p_a.start > p_b.start; });
  int previousStart = selectedText.text.size();
  for (const auto &rewrite : rewrites) {
    if (rewrite.start < 0 || rewrite.end < rewrite.start || rewrite.end > previousStart) {
      return fail(VXCORE_ERR_UNSUPPORTED,
                  tr("Resource rewrite ranges overlap or cannot be verified"));
    }
    selectedText.text.replace(rewrite.start, rewrite.end - rewrite.start, rewrite.text);
    previousStart = rewrite.start;
  }
  if (!rewrites.isEmpty()) {
    const auto linkIdentity = [](const vte::MarkdownLink &p_link) {
      return QJsonDocument(QJsonArray{p_link.m_isImage, p_link.m_urlInLink, p_link.m_title,
                                      p_link.m_width, p_link.m_height})
          .toJson(QJsonDocument::Compact);
    };
    QMap<QByteArray, int> expected;
    QMap<QByteArray, int> actual;
    for (const auto &link : expectedLinks) {
      ++expected[linkIdentity(link)];
    }
    for (const auto &link : vte::MarkdownUtils::fetchResourceLinks(
             selectedText.text, QFileInfo(source).absolutePath(), allLinkFlags)) {
      if (!link.m_rewriteSupported) {
        return fail(VXCORE_ERR_UNSUPPORTED, tr("The rewritten resource links cannot be verified"));
      }
      ++actual[linkIdentity(link)];
    }
    if (actual != expected) {
      return fail(VXCORE_ERR_UNSUPPORTED, tr("A resource rewrite changed the Markdown structure"));
    }
  }
  if (rewrites.isEmpty()) {
    plan.m_body = p_currentBody;
  } else if (!selectedText.encode(selectedText.text, plan.m_body)) {
    return fail(VXCORE_ERR_UNSUPPORTED, tr("The resource links cannot be encoded losslessly"));
  }
  plan.m_resourcePlan.insert(QLatin1String(vxcore::kJsonKeyEditorType), editorType);
  plan.m_resourcePlan.insert(QLatin1String(vxcore::kJsonKeySourceSha256),
                             QString::fromLatin1(sourceHash));
  plan.m_resourcePlan.insert(QLatin1String(vxcore::kJsonKeyResources), resourceJson);
  plan.m_error = VXCORE_OK;
  qCInfo(lcNoteEncryption).noquote().nospace()
      << "phase=plan_complete note_id=" << fileId.toString(QUuid::WithoutBraces)
      << " resources=" << resources.size() << " owned_resources=" << ownedCount
      << " outside_owned_resources=" << outsideCount
      << " link_unverified_resources=" << linkUnverifiedCount << " shared_resources=" << sharedCount
      << " uncertain_resources=" << uncertainCount << " reference_scan_complete=" << !retainAll
      << " retained_originals=" << plan.m_retainedOriginals.size();
  return plan;
}

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

  if (!p_buffer.isValid() || p_buffer.isEncrypted()) {
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
