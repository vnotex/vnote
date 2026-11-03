#include "vnoteapibackend.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/noteoperationservice.h>
#include <core/services/searchservice.h>
#include <vxcore/notebook_json_keys.h>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vnotex {
namespace {
constexpr qint64 kMaxSnapshotBytes = 16 * 1024 * 1024;
constexpr int kMaxReadWorkers = 4;
constexpr int kMaxSearchFiles = 1000;
constexpr int kMaxScannedNodes = 10000;
constexpr qint64 kMaxSearchBytes = 64 * 1024 * 1024;
using Clock = std::chrono::steady_clock;

ApiResult failure(int p_status, const char *p_code, const char *p_message) {
  ApiResult result;
  result.error = {p_status, QString::fromLatin1(p_code), QString::fromLatin1(p_message)};
  return result;
}
ApiResult unavailable() { return failure(404, "not_found", "The resource is unavailable."); }
ApiResult changed() {
  return failure(412, "precondition_failed", "The resource changed; read a fresh snapshot.");
}
QString version(const char *p_kind, const QJsonObject &p_facts) {
  return QString::fromLatin1(p_kind) + QLatin1Char('-') +
         QString::fromLatin1(
             QCryptographicHash::hash(QJsonDocument(p_facts).toJson(QJsonDocument::Compact),
                                      QCryptographicHash::Sha256)
                 .toHex());
}
bool authorized(const ApiRequestContext &p_context) {
  return p_context.authorized && p_context.authorized() &&
         (!p_context.cancelled || !p_context.cancelled->load(std::memory_order_acquire));
}
bool within(const QString &p_path, const QString &p_root) {
#ifdef Q_OS_WIN
  constexpr auto sensitivity = Qt::CaseInsensitive;
#else
  constexpr auto sensitivity = Qt::CaseSensitive;
#endif
  const QString root = QDir::cleanPath(QDir::fromNativeSeparators(p_root));
  const QString path = QDir::cleanPath(QDir::fromNativeSeparators(p_path));
  return !p_root.isEmpty() && (path.compare(root, sensitivity) == 0 ||
                               path.startsWith(root + QLatin1Char('/'), sensitivity));
}
bool ordinaryRelativePath(const QString &p_path, bool p_root = false) {
  if (p_path.isEmpty() || p_path == QLatin1String("."))
    return p_root;
  if (QDir::isAbsolutePath(p_path) || p_path.contains(QLatin1Char('\\')) ||
      p_path.contains(QLatin1Char(':')) || p_path.contains(QChar::Null))
    return false;
  for (const auto &part : p_path.split(QLatin1Char('/'))) {
    // Hidden notebook/control trees are not note resources, even in a raw notebook.
    if (part.isEmpty() || part.startsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char('.')) ||
        part.endsWith(QLatin1Char(' ')))
      return false;
  }
  return true;
}
bool safeComponents(const QString &p_root, const QString &p_relative) {
  QString path = QDir::cleanPath(p_root);
  if (!QFileInfo(path).isDir() || QFileInfo(path).isSymLink())
    return false;
#ifdef Q_OS_WIN
  const auto isReparse = [](const QString &p_path) {
    const auto attrs = GetFileAttributesW(reinterpret_cast<LPCWSTR>(p_path.utf16()));
    return attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
  };
  if (isReparse(path))
    return false;
#endif
  for (const auto &part : p_relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    if (part == QLatin1String("."))
      continue;
    path = QDir(path).filePath(part);
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink())
      return false;
#ifdef Q_OS_WIN
    if (isReparse(path))
      return false;
#endif
  }
  return within(QFileInfo(path).canonicalFilePath(), QFileInfo(p_root).canonicalFilePath());
}
QString lineEnding(const QString &p_text) {
  const qsizetype crlf = p_text.count(QStringLiteral("\r\n"));
  const qsizetype lf = p_text.count(QLatin1Char('\n')) - crlf;
  const qsizetype cr = p_text.count(QLatin1Char('\r')) - crlf;
  if ((crlf > 0) + (lf > 0) + (cr > 0) > 1)
    return QStringLiteral("mixed");
  return crlf ? QStringLiteral("CRLF") : cr ? QStringLiteral("CR") : QStringLiteral("LF");
}

struct DiskSnapshot {
  ApiResult error;
  QByteArray digest;
  QString text, encoding, ending;
};

// Only this helper touches ordinary file bytes. The owner supplied an indexed, authorized
// node, a real notebook root and its resolved relative path. No external-path fallback exists.
DiskSnapshot readSaved(const QString &p_root, const QString &p_path, const QString &p_encoding,
                       const std::shared_ptr<std::atomic_bool> &p_cancelled) {
  DiskSnapshot result;
  result.error = unavailable();
  if (!ordinaryRelativePath(p_path) || !safeComponents(p_root, p_path))
    return result;
  QByteArray bytes;
#ifdef Q_OS_WIN
  const QString absolute = QDir(p_root).filePath(p_path);
  HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(absolute.utf16()), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return result;
  const auto closeHandle = qScopeGuard([&] { CloseHandle(handle); });
  BY_HANDLE_FILE_INFORMATION before{}, after{};
  if (!GetFileInformationByHandle(handle, &before) ||
      (before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    return result;
  wchar_t actual[32768];
  const DWORD length = GetFinalPathNameByHandleW(handle, actual, 32768, FILE_NAME_NORMALIZED);
  if (!length || length >= 32768)
    return result;
  QString finalPath = QString::fromWCharArray(actual, int(length));
  if (finalPath.startsWith(QLatin1String("\\\\?\\UNC\\")))
    finalPath = QStringLiteral("\\\\") + finalPath.mid(8);
  else if (finalPath.startsWith(QLatin1String("\\\\?\\")))
    finalPath.remove(0, 4);
  if (!within(finalPath, QFileInfo(p_root).canonicalFilePath()) || !safeComponents(p_root, p_path))
    return result;
  const quint64 size = (quint64(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
  if (size > quint64(kMaxSnapshotBytes)) {
    result.error = failure(413, "note_too_large", "The note exceeds the snapshot limit.");
    return result;
  }
  bytes.resize(qsizetype(size));
  qint64 offset = 0;
  while (offset < bytes.size()) {
    if (p_cancelled && p_cancelled->load())
      return result;
    DWORD count = 0;
    if (!ReadFile(handle, bytes.data() + offset, DWORD(qMin<qint64>(65536, bytes.size() - offset)),
                  &count, nullptr) ||
        !count)
      return result;
    offset += count;
  }
  if (!GetFileInformationByHandle(handle, &after) || before.nFileSizeHigh != after.nFileSizeHigh ||
      before.nFileSizeLow != after.nFileSizeLow ||
      CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) != 0) {
    result.error = changed();
    return result;
  }
#else
  int dir = ::open(QFile::encodeName(p_root).constData(),
                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (dir < 0)
    return result;
  const auto closeDir = qScopeGuard([&] { ::close(dir); });
  const auto parts = p_path.split(QLatin1Char('/'));
  for (qsizetype idx = 0; idx + 1 < parts.size(); ++idx) {
    const int next = ::openat(dir, QFile::encodeName(parts[idx]).constData(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0)
      return result;
    ::close(dir);
    dir = next;
  }
  const int fd = ::openat(dir, QFile::encodeName(parts.last()).constData(),
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return result;
  const auto closeFile = qScopeGuard([&] { ::close(fd); });
  struct stat before{}, after{};
  if (::fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size < 0)
    return result;
  if (before.st_size > kMaxSnapshotBytes) {
    result.error = failure(413, "note_too_large", "The note exceeds the snapshot limit.");
    return result;
  }
  bytes.resize(qsizetype(before.st_size));
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    if (p_cancelled && p_cancelled->load())
      return result;
    const auto count =
        ::read(fd, bytes.data() + offset, size_t(qMin<qsizetype>(65536, bytes.size() - offset)));
    if (count <= 0)
      return result;
    offset += count;
  }
  if (::fstat(fd, &after) != 0 || before.st_size != after.st_size ||
      before.st_mtime != after.st_mtime || before.st_ctime != after.st_ctime ||
      !safeComponents(p_root, p_path)) {
    result.error = changed();
    return result;
  }
#ifdef Q_OS_MACOS
  if (before.st_mtimespec.tv_nsec != after.st_mtimespec.tv_nsec ||
      before.st_ctimespec.tv_nsec != after.st_ctimespec.tv_nsec) {
#else
  if (before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
#endif
    result.error = changed();
    return result;
  }
  struct stat current{};
  if (::fstatat(dir, QFile::encodeName(parts.last()).constData(), &current, AT_SYMLINK_NOFOLLOW) !=
          0 ||
      current.st_dev != before.st_dev || current.st_ino != before.st_ino) {
    result.error = changed();
    return result;
  }
#endif
  auto *codec = QTextCodec::codecForName(p_encoding.toLatin1());
  if (!codec) {
    result.error = failure(422, "unsupported_encoding", "The note encoding is unsupported.");
    return result;
  }
  QTextCodec::ConverterState decode(QTextCodec::IgnoreHeader);
  result.text = codec->toUnicode(bytes.constData(), bytes.size(), &decode);
  QTextCodec::ConverterState encode(QTextCodec::IgnoreHeader);
  const auto roundtrip = codec->fromUnicode(result.text.constData(), result.text.size(), &encode);
  if (decode.invalidChars || decode.remainingChars || encode.invalidChars || roundtrip != bytes ||
      result.text.contains(QChar::Null)) {
    result.error =
        failure(422, "unsupported_encoding", "The note cannot be represented as lossless text.");
    return result;
  }
  result.encoding = QString::fromLatin1(codec->name());
  result.ending = lineEnding(result.text);
  result.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  result.error = {};
  return result;
}

QString cursor(const QString &p_snapshot, qint64 p_offset) {
  return QString::fromLatin1(
      QJsonDocument(QJsonObject{{"snapshot", p_snapshot}, {"offset", p_offset}})
          .toJson(QJsonDocument::Compact)
          .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
bool parseCursor(const QString &p_cursor, const QString &p_snapshot, qint64 *p_offset) {
  const auto decoded = QByteArray::fromBase64Encoding(
      p_cursor.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded)
    return false;
  const auto obj = QJsonDocument::fromJson(decoded.decoded).object();
  if (obj.size() != 2 || obj.value("snapshot").toString() != p_snapshot ||
      !obj.value("offset").isDouble())
    return false;
  const double value = obj.value("offset").toDouble(-1);
  if (value < 0 || value > double(kMaxSnapshotBytes) || value != qint64(value))
    return false;
  *p_offset = qint64(value);
  return true;
}
} // namespace

struct VNoteApiBackend::Impl {
  struct Node {
    QString notebook, id, path, root, name, type, encoding = QStringLiteral("UTF-8");
    QString bufferId;
    bool folder = false, readable = false, readOnly = true, trash = false, dirty = false;
    QJsonObject metadata;
  };
  struct SearchTarget {
    QString notebook;
    QJsonArray paths;
    QHash<QString, QString> ids;
    QList<Node> nodes;
    QHash<QString, QByteArray> digests;
  };
  struct Pending {
    ApiCommand command;
    ApiRequestContext context;
    Completion complete;
    ApiResult committedOutcome;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(20);
    QList<SearchTarget> targets;
    SearchTarget current;
    QJsonArray items;
    bool truncated = false;
    int searchToken = 0;
    quint64 operationToken = 0;
  };
  VNoteApiBackend *owner;
  ServiceLocator &services;
  NoteOperationService operations;
  QHash<quint64, std::shared_ptr<Pending>> pending;
  QList<quint64> searchQueue;
  quint64 nextId = 1, activeSearch = 0;
  int readers = 0;
  bool stopped = false;
  QTimer timer;

  Impl(VNoteApiBackend *p_owner, ServiceLocator &p_services)
      : owner(p_owner), services(p_services), operations(p_services), timer(p_owner) {
    QObject::connect(&operations, &NoteOperationService::finished, owner,
                     [this](quint64 token, const NoteOperationResult &result) {
                       operationFinished(token, result);
                     });
    if (auto *search = services.get<SearchService>()) {
      QObject::connect(
          search, &SearchService::searchFinished, owner,
          [this](int token, const SearchResult &result) { searchFinished(token, result); });
      QObject::connect(
          search, &SearchService::searchFailed, owner, [this](int token, const Error &) {
            if (activeSearch && pending.value(activeSearch)->searchToken == token)
              finishSearch(failure(503, "search_failed", "The search could not be completed."));
          });
      QObject::connect(search, &SearchService::searchCancelled, owner, [this](int token) {
        if (activeSearch && pending.value(activeSearch)->searchToken == token)
          finishSearch(failure(503, "cancelled", "The search was cancelled."));
      });
    }
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, owner, [this] { expire(); });
    timer.start();
  }

  QStringList excludedRoots() const {
    QStringList roots{QCoreApplication::applicationDirPath()};
    if (const auto *config = services.get<ConfigMgr2>())
      roots << config->getAppDataPath() << config->getUserConfigPath();
    return roots;
  }
  bool allowedPath(const QString &root, const QString &path, bool folder) const {
    const auto *config = services.get<ConfigMgr2>();
    if (!config || config->getUserConfigPath().isEmpty() || config->getAppDataPath().isEmpty())
      return false;
    if (!ordinaryRelativePath(path, folder) || !safeComponents(root, path))
      return false;
    const auto absolute = QFileInfo(QDir(root).filePath(path)).canonicalFilePath();
    for (const auto &excluded : excludedRoots()) {
      if (within(absolute, excluded) || within(absolute, QFileInfo(excluded).canonicalFilePath()))
        return false;
    }
    return true;
  }
  QJsonObject notebook(const QString &id, const ApiRequestContext &context) const {
    if (!context.principal.notebookIds.contains(id))
      return {};
    auto *core = services.get<NotebookCoreService>();
    if (!core)
      return {};
    return core->getNotebookConfig(id);
  }
  ApiResult resolve(const QString &nb, const QString &id, const ApiRequestContext &context,
                    Node *out, bool allowFolder = false) const {
    const auto book = notebook(nb, context);
    if (book.isEmpty())
      return unavailable();
    auto *core = services.get<NotebookCoreService>();
    if (id.isEmpty())
      return unavailable();
    QString path = core->getNodePathById(nb, id);
    if (path.isEmpty() && allowFolder) {
      // vxcore's path-by-UUID API represents the root as empty/not-found. Only
      // the independently read matching root UUID can authorize that identity.
      const auto rootInfo = core->getFileInfo(nb, QString());
      if (rootInfo.value(QLatin1String(vxcore::kJsonKeyId)).toString() == id &&
          rootInfo.value(QLatin1String(vxcore::kJsonKeyType)).toString() == QLatin1String("folder"))
        path = QStringLiteral(".");
    }
    if (path.isEmpty())
      return unavailable();
    VxCoreError error = VXCORE_OK;
    const auto info = core->getFileInfo(nb, path, &error);
    if (error != VXCORE_OK || info.value(QLatin1String(vxcore::kJsonKeyId)).toString() != id)
      return unavailable();
    const bool folder =
        info.value(QLatin1String(vxcore::kJsonKeyType)).toString() == QLatin1String("folder");
    if (info.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
        path.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive))
      return failure(422, "protected_note_unavailable",
                     "Protected notes are unavailable through this API.");
    if ((!folder &&
         info.value(QLatin1String(vxcore::kJsonKeyType)).toString() != QLatin1String("file")) ||
        (folder && !allowFolder))
      return failure(422, "unsupported_note_type", "This resource is not an ordinary note.");
    const auto root = core->buildAbsolutePath(nb, QStringLiteral("."));
    if (root.isEmpty() || !allowedPath(root, path, folder))
      return unavailable();
    Node node;
    node.notebook = nb;
    node.id = id;
    node.path = path;
    node.root = QFileInfo(root).canonicalFilePath();
    node.name = info.value(QLatin1String(vxcore::kJsonKeyName)).toString();
    node.folder = folder;
    node.readOnly = core->isNotebookReadOnly(nb);
    node.trash = !node.readOnly && !folder && !core->getRecycleBinPath(nb).isEmpty();
    if (auto *types = services.get<FileTypeCoreService>()) {
      node.type = types->getFileType(path).m_typeName;
      node.readable =
          !folder && (node.type == QLatin1String("Markdown") || node.type == QLatin1String("Text"));
    }
    if (auto *buffers = services.get<BufferService>()) {
      for (const auto &entry : buffers->listBuffers()) {
        const auto object = entry.toObject();
        if (object.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() == nb &&
            object.value("filePath").toString() == path) {
          node.bufferId = object.value(QLatin1String(vxcore::kJsonKeyId)).toString();
          node.encoding = buffers->bufferEncoding(node.bufferId);
          node.dirty = buffers->isDirty(node.bufferId) || buffers->isModified(node.bufferId);
          node.readOnly = node.readOnly || buffers->isBufferReadOnly(node.bufferId);
          break;
        }
      }
    }
    node.trash = node.trash && node.readable && !node.readOnly;
    node.metadata = {
        {QLatin1String(vxcore::kJsonKeyId), id},
        {QLatin1String(vxcore::kJsonKeyNotebookId), nb},
        {QLatin1String(vxcore::kJsonKeyName), node.name},
        {"path", path},
        {QLatin1String(vxcore::kJsonKeyType), folder ? QStringLiteral("folder") : node.type},
        {"contentReadable", node.readable},
        {QLatin1String(vxcore::kJsonKeyReadOnly), node.readOnly},
        {"capabilities", QJsonObject{{"stableId", true},
                                     {"readContent", node.readable},
                                     {"writeContent", node.readable && !node.readOnly},
                                     {"rename", !folder && !node.readOnly},
                                     {"trash", node.trash && !node.readOnly}}}};
    node.metadata.insert("metadataVersion", version("metadata", node.metadata));
    *out = std::move(node);
    return {};
  }
  QString contentVersion(const Node &node, const DiskSnapshot &disk) const {
    return version("saved", {{QLatin1String(vxcore::kJsonKeyId), node.id},
                             {QLatin1String(vxcore::kJsonKeyNotebookId), node.notebook},
                             {"path", node.path},
                             {"sha256", QString::fromLatin1(disk.digest.toHex())},
                             {"encoding", disk.encoding},
                             {"lineEnding", disk.ending}});
  }
  QString stateVersion(const Node &node, const DiskSnapshot &disk) const {
    return version("state", {{"metadataVersion", node.metadata.value("metadataVersion")},
                             {"contentVersion", contentVersion(node, disk)}});
  }
  void finish(quint64 id, ApiResult result) {
    const auto item = pending.take(id);
    if (!item)
      return;
    if (!result.committed && item->committedOutcome.committed)
      result = item->committedOutcome;
    if (!result.committed && !authorized(item->context))
      result = failure(503, "cancelled", "The request is no longer authorized or was cancelled.");
    auto complete = std::move(item->complete);
    if (complete)
      complete(std::move(result));
  }
  void saved(quint64 id, Node node,
             std::function<void(const Node &, const DiskSnapshot &)> callback) {
    if (readers >= kMaxReadWorkers) {
      finish(id, failure(429, "read_capacity", "Too many note snapshots are in progress."));
      return;
    }
    ++readers;
    auto *watcher = new QFutureWatcher<DiskSnapshot>(owner);
    const auto item = pending.value(id);
    QObject::connect(watcher, &QFutureWatcher<DiskSnapshot>::finished, owner,
                     [this, watcher, id, node, callback = std::move(callback)] {
                       --readers;
                       const auto disk = watcher->result();
                       watcher->deleteLater();
                       const auto item = pending.value(id);
                       if (!item || stopped)
                         return;
                       if (!authorized(item->context)) {
                         finish(id, {});
                         return;
                       }
                       Node fresh;
                       auto check = resolve(node.notebook, node.id, item->context, &fresh);
                       if (check.error.status) {
                         finish(id, check);
                         return;
                       }
                       if (fresh.metadata != node.metadata || fresh.encoding != node.encoding ||
                           fresh.root != node.root) {
                         finish(id, changed());
                         return;
                       }
                       if (disk.error.error.status) {
                         finish(id, disk.error);
                         return;
                       }
                       callback(fresh, disk);
                     });
    watcher->setFuture(
        QtConcurrent::run(readSaved, node.root, node.path, node.encoding, item->context.cancelled));
  }

  void listNodes(quint64 id) {
    const auto item = pending.value(id);
    const auto &cmd = item->command;
    if (notebook(cmd.notebookId, item->context).isEmpty()) {
      finish(id, unavailable());
      return;
    }
    auto *core = services.get<NotebookCoreService>();
    QString path;
    if (!cmd.parentId.isEmpty()) {
      Node parent;
      auto result = resolve(cmd.notebookId, cmd.parentId, item->context, &parent, true);
      if (result.error.status || !parent.folder) {
        finish(id, result.error.status ? result : unavailable());
        return;
      }
      path = parent.path == QLatin1String(".") ? QString() : parent.path;
    }
    const auto root = core->buildAbsolutePath(cmd.notebookId, QStringLiteral("."));
    if (!allowedPath(root, path, true)) {
      finish(id, unavailable());
      return;
    }
    VxCoreError error = VXCORE_OK;
    const auto children = core->listFolderChildren(cmd.notebookId, path, &error);
    if (error != VXCORE_OK) {
      finish(id, unavailable());
      return;
    }
    QList<QJsonObject> nodes;
    for (const auto key : {vxcore::kJsonKeyFolders, vxcore::kJsonKeyFiles}) {
      for (const auto &value : children.value(QLatin1String(key)).toArray()) {
        const auto child = value.toObject();
        if (child.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
            child.value(QLatin1String(vxcore::kJsonKeyName))
                .toString()
                .endsWith(QLatin1String(".vne"), Qt::CaseInsensitive))
          continue;
        if (child.value(QLatin1String(vxcore::kJsonKeyId)).toString().isEmpty()) {
          const auto name = child.value(QLatin1String(vxcore::kJsonKeyName)).toString();
          const auto relative = path.isEmpty() ? name : path + QLatin1Char('/') + name;
          const bool folder = QLatin1String(key) == QLatin1String(vxcore::kJsonKeyFolders);
          if (!ordinaryRelativePath(name) || name.contains(QLatin1Char('/')) ||
              !allowedPath(root, relative, folder))
            continue;
          const auto info = core->getFileInfo(cmd.notebookId, relative);
          if (info.isEmpty() ||
              info.value(QLatin1String(vxcore::kJsonKeyName)).toString() != name ||
              info.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool())
            continue;
          // Display-only indexed entries cannot be addressed by the API. Never invent a
          // persistent identifier from their path or silently grant mutation capability.
          nodes.append(QJsonObject{
              {QLatin1String(vxcore::kJsonKeyId), QJsonValue::Null},
              {QLatin1String(vxcore::kJsonKeyNotebookId), cmd.notebookId},
              {QLatin1String(vxcore::kJsonKeyName), name},
              {"path", relative},
              {QLatin1String(vxcore::kJsonKeyType), folder ? "folder" : "file"},
              {"contentReadable", false},
              {QLatin1String(vxcore::kJsonKeyReadOnly), core->isNotebookReadOnly(cmd.notebookId)},
              {"capabilities", QJsonObject{{"stableId", false},
                                           {"readContent", false},
                                           {"writeContent", false},
                                           {"rename", false},
                                           {"trash", false}}}});
          if (nodes.size() > kMaxScannedNodes) {
            finish(id, failure(413, "listing_too_large",
                               "The folder exceeds the listing snapshot limit."));
            return;
          }
          continue;
        }
        Node node;
        if (!resolve(cmd.notebookId, child.value(QLatin1String(vxcore::kJsonKeyId)).toString(),
                     item->context, &node, true)
                 .error.status)
          nodes.append(node.metadata);
        if (nodes.size() > kMaxScannedNodes) {
          finish(id, failure(413, "listing_too_large",
                             "The folder exceeds the listing snapshot limit."));
          return;
        }
      }
    }
    std::sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) {
      const auto left = a.value(QLatin1String(vxcore::kJsonKeyId)).toString(),
                 right = b.value(QLatin1String(vxcore::kJsonKeyId)).toString();
      return left == right ? a.value("path").toString() < b.value("path").toString() : left < right;
    });
    QJsonArray all;
    for (const auto &node : nodes)
      all.append(node);
    const auto snapshot =
        version("nodes", {{QLatin1String(vxcore::kJsonKeyNotebookId), cmd.notebookId},
                          {"parentId", cmd.parentId},
                          {"items", all}});
    qint64 offset = 0;
    if (!cmd.cursor.isEmpty() && !parseCursor(cmd.cursor, snapshot, &offset)) {
      finish(id, changed());
      return;
    }
    if (offset > nodes.size()) {
      finish(id, changed());
      return;
    }
    QJsonArray page;
    const qint64 end = qMin<qint64>(nodes.size(), offset + qBound(1, cmd.limit, 200));
    for (; offset < end; ++offset)
      page.append(nodes[offset]);
    ApiResult result;
    result.data = {{"items", page},
                   {"nextCursor", end < nodes.size() ? QJsonValue(cursor(snapshot, end))
                                                     : QJsonValue(QJsonValue::Null)}};
    finish(id, result);
  }
  void readContent(quint64 id, const Node &node, const DiskSnapshot &disk) {
    const auto item = pending.value(id);
    const auto &cmd = item->command;
    QString text = disk.text;
    QString view = QStringLiteral("saved");
    QString token = contentVersion(node, disk);
    if (cmd.view == ApiContentView::Live && !node.bufferId.isEmpty()) {
      if (auto *buffers = services.get<BufferService>()) {
        QString live;
        bool hasWriter = false;
        if (buffers->captureActiveWriterContent(node.bufferId, &live, &hasWriter)) {
          text = std::move(live);
          view = QStringLiteral("live");
          token = version("live", {{QLatin1String(vxcore::kJsonKeyId), node.id},
                                   {QLatin1String(vxcore::kJsonKeyNotebookId), node.notebook},
                                   {"text", text},
                                   {"savedVersion", token},
                                   {"dirty", node.dirty}});
        } else if (hasWriter) {
          finish(id, failure(503, "live_capture_failed",
                             "The live editor could not provide a snapshot."));
          return;
        }
      }
    }
    const auto utf8 = text.toUtf8();
    if (utf8.size() > kMaxSnapshotBytes) {
      finish(id, failure(413, "note_too_large", "The note exceeds the snapshot limit."));
      return;
    }
    qint64 offset = cmd.offset;
    if (!cmd.cursor.isEmpty()) {
      qint64 continuation = 0;
      if (!parseCursor(cmd.cursor, token, &continuation) ||
          (offset != 0 && offset != continuation)) {
        finish(id, changed());
        return;
      }
      offset = continuation;
    }
    const auto boundary = [&utf8](qint64 pos) {
      return pos == utf8.size() || (quint8(utf8[pos]) & 0xc0) != 0x80;
    };
    if (offset < 0 || offset > utf8.size() || !boundary(offset)) {
      finish(id, failure(400, "invalid_offset", "The offset must be a UTF-8 byte boundary."));
      return;
    }
    qint64 end = qMin<qint64>(utf8.size(), offset + qBound(1, cmd.limit, 1048576));
    while (end > offset && !boundary(end))
      --end;
    if (end == offset && offset < utf8.size()) {
      finish(id, failure(400, "invalid_limit", "The limit must fit the next UTF-8 character."));
      return;
    }
    ApiResult result;
    result.data = {{QLatin1String(vxcore::kJsonKeyId), node.id},
                   {QLatin1String(vxcore::kJsonKeyNotebookId), node.notebook},
                   {"view", view},
                   {"text", QString::fromUtf8(utf8.constData() + offset, end - offset)},
                   {"encoding", disk.encoding},
                   {"lineEnding", view == QLatin1String("live") ? lineEnding(text) : disk.ending},
                   {"dirty", node.dirty},
                   {"contentVersion", token},
                   {"offset", offset},
                   {"returnedBytes", end - offset},
                   {"totalBytes", utf8.size()},
                   {"nextCursor", end < utf8.size() ? QJsonValue(cursor(token, end))
                                                    : QJsonValue(QJsonValue::Null)}};
    if (view == QLatin1String("saved"))
      result.data.insert("noteStateVersion", stateVersion(node, disk));
    finish(id, result);
  }

  void submitOperation(quint64 id, const Node *node, const DiskSnapshot *disk = nullptr) {
    const auto item = pending.value(id);
    if (!item || !authorized(item->context)) {
      finish(id, {});
      return;
    }
    const auto &cmd = item->command;
    NoteOperationRequest request;
    request.notebookId = cmd.notebookId;
    request.nodeId = cmd.nodeId;
    request.parentId = cmd.parentId;
    request.name = cmd.name;
    request.text = cmd.text;
    request.authorized = item->context.authorized;
    request.cancelled = item->context.cancelled;
    if (node) {
      request.relativePath = node->path;
      request.expectedPath = node->path;
      if (node->readOnly) {
        finish(id, failure(403, "read_only", "The resource is read-only."));
        return;
      }
      QString expected;
      if (cmd.kind == ApiCommandKind::RenameNote)
        expected = node->metadata.value("metadataVersion").toString();
      else if (disk)
        expected = cmd.kind == ApiCommandKind::TrashNote ? stateVersion(*node, *disk)
                                                         : contentVersion(*node, *disk);
      if (cmd.expectedVersion.isEmpty()) {
        finish(id, failure(428, "precondition_required", "A resource version is required."));
        return;
      }
      if (expected != cmd.expectedVersion) {
        finish(id, changed());
        return;
      }
      if (disk)
        request.expectedFileSha256 = disk->digest;
    }
    switch (cmd.kind) {
    case ApiCommandKind::CreateNote:
      request.kind = NoteOperationKind::Create;
      break;
    case ApiCommandKind::ReplaceContent:
      request.kind = NoteOperationKind::Replace;
      break;
    case ApiCommandKind::AppendContent:
      request.kind = NoteOperationKind::Append;
      break;
    case ApiCommandKind::RenameNote:
      request.kind = NoteOperationKind::Rename;
      break;
    case ApiCommandKind::TrashNote:
      request.kind = NoteOperationKind::Trash;
      break;
    default:
      return;
    }
    item->operationToken = operations.execute(std::move(request));
  }
  void operationFinished(quint64 token, const NoteOperationResult &domain) {
    quint64 id = 0;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
      if (it.value()->operationToken == token) {
        id = it.key();
        break;
      }
    if (!id)
      return;
    const auto item = pending.value(id);
    ApiResult result;
    result.committed = domain.committed;
    result.saved = domain.saved;
    result.data = {{"recoveryRequired", domain.recoveryRequired}};
    if (!domain.nodeId.isEmpty())
      result.data.insert(QLatin1String(vxcore::kJsonKeyId), domain.nodeId);
    if (!domain.notebookId.isEmpty())
      result.data.insert(QLatin1String(vxcore::kJsonKeyNotebookId), domain.notebookId);
    if (!domain.errorCode.isEmpty()) {
      const auto &code = domain.errorCode;
      int status = 500;
      if (code == QLatin1String("precondition_failed"))
        status = 412;
      else if (code == QLatin1String("not_found"))
        status = 404;
      else if (code == QLatin1String("read_only") || code == QLatin1String("forbidden"))
        status = 403;
      else if (code == QLatin1String("protected_note_unavailable"))
        status = 422;
      else if (code == QLatin1String("unsupported_note_type") ||
               code == QLatin1String("unsupported_encoding") ||
               code == QLatin1String("unsupported_trash") ||
               code == QLatin1String("unsupported_operation"))
        status = 422;
      else if (code == QLatin1String("invalid_name"))
        status = 400;
      else if (code == QLatin1String("unsaved_changes") || code == QLatin1String("resource_busy") ||
               code == QLatin1String("name_conflict") ||
               code == QLatin1String("comments_not_saved") ||
               code == QLatin1String("recovery_required"))
        status = 409;
      else if (code == QLatin1String("cancelled") || code == QLatin1String("unavailable") ||
               code == QLatin1String("stopping") || code == QLatin1String("save_failed"))
        status = 503;
      result.error = {status, code,
                      QStringLiteral("The note operation did not complete without errors.")};
    }
    if (!domain.committed || item->command.kind == ApiCommandKind::TrashNote ||
        !authorized(item->context)) {
      finish(id, result);
      return;
    }
    Node node;
    if (resolve(domain.notebookId, domain.nodeId, item->context, &node).error.status) {
      // The mutation outcome is authoritative even if later reconciliation cannot read it.
      result.data.insert("refreshRequired", true);
      finish(id, result);
      return;
    }
    result.data.insert("metadataVersion", node.metadata.value("metadataVersion"));
    if (!node.readable || !domain.saved) {
      finish(id, result);
      return;
    }
    result.data.insert("refreshRequired", true);
    item->committedOutcome = result;
    item->operationToken = 0;
    saved(id, node,
          [this, id, result, digest = domain.fileSha256](const Node &fresh,
                                                         const DiskSnapshot &disk) mutable {
            if (!digest.isEmpty() && disk.digest != digest) {
              finish(id, result);
              return;
            }
            result.data.remove("refreshRequired");
            result.data.insert("contentVersion", contentVersion(fresh, disk));
            result.data.insert("noteStateVersion", stateVersion(fresh, disk));
            finish(id, result);
          });
  }

  void prepareSearch(quint64 id) {
    const auto item = pending.value(id);
    const auto &cmd = item->command;
    auto *core = services.get<NotebookCoreService>();
    if (!core || !services.get<SearchService>()) {
      finish(id, failure(503, "unavailable", "Search is unavailable."));
      return;
    }
    QStringList books =
        cmd.notebookIds.isEmpty() ? item->context.principal.notebookIds.values() : cmd.notebookIds;
    std::sort(books.begin(), books.end());
    int files = 0, scanned = 0;
    qint64 totalBytes = 0;
    for (const auto &nb : books) {
      if (notebook(nb, item->context).isEmpty()) {
        if (!cmd.notebookIds.isEmpty()) {
          finish(id, unavailable());
          return;
        }
        continue;
      }
      SearchTarget target;
      target.notebook = nb;
      QList<QString> folders{QString()};
      QSet<QString> seen;
      while (!folders.isEmpty() && scanned < kMaxScannedNodes && files < kMaxSearchFiles) {
        const auto folder = folders.takeFirst();
        if (seen.contains(folder))
          continue;
        seen.insert(folder);
        const auto root = core->buildAbsolutePath(nb, QStringLiteral("."));
        if (!allowedPath(root, folder, true))
          continue;
        const auto children = core->listFolderChildren(nb, folder);
        for (const auto key : {vxcore::kJsonKeyFolders, vxcore::kJsonKeyFiles}) {
          for (const auto &value : children.value(QLatin1String(key)).toArray()) {
            if (++scanned > kMaxScannedNodes || files >= kMaxSearchFiles) {
              item->truncated = true;
              break;
            }
            const auto child = value.toObject();
            if (child.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
                child.value(QLatin1String(vxcore::kJsonKeyName))
                    .toString()
                    .endsWith(QLatin1String(".vne"), Qt::CaseInsensitive))
              continue;
            Node node;
            if (resolve(nb, child.value(QLatin1String(vxcore::kJsonKeyId)).toString(),
                        item->context, &node, true)
                    .error.status)
              continue;
            if (node.folder) {
              folders.append(node.path);
              continue;
            }
            if (cmd.searchContent && !node.readable)
              continue;
            if (!cmd.searchContent && !node.name.contains(cmd.query, Qt::CaseInsensitive))
              continue;
            quint64 size = 0;
            if (cmd.searchContent && (core->getFileSize(nb, node.path, size) != VXCORE_OK ||
                                      size > quint64(kMaxSnapshotBytes)))
              continue;
            if (totalBytes + qint64(size) > kMaxSearchBytes) {
              item->truncated = true;
              continue;
            }
            totalBytes += size;
            ++files;
            target.paths.append(node.path);
            target.ids.insert(node.path, node.id);
            target.nodes.append(std::move(node));
          }
        }
      }
      if (!folders.isEmpty())
        item->truncated = true;
      if (!target.paths.isEmpty())
        item->targets.append(std::move(target));
    }
    searchQueue.append(id);
    startNextSearch();
  }
  void startNextSearch() {
    if (stopped || activeSearch)
      return;
    while (!searchQueue.isEmpty()) {
      activeSearch = searchQueue.takeFirst();
      if (pending.contains(activeSearch)) {
        searchStep();
        return;
      }
      activeSearch = 0;
    }
  }
  void searchStep() {
    const auto item = pending.value(activeSearch);
    if (!item)
      return;
    if (!authorized(item->context)) {
      finishSearch({});
      return;
    }
    if (item->targets.isEmpty() || item->items.size() >= qBound(1, item->command.limit, 50)) {
      ApiResult result;
      result.data = {{"items", item->items},
                     {"truncated", item->truncated || !item->targets.isEmpty()},
                     {"view", "saved"}};
      finishSearch(result);
      return;
    }
    item->current = item->targets.takeFirst();
    if (!item->command.searchContent) {
      submitSearch();
      return;
    }
    if (readers >= kMaxReadWorkers) {
      finishSearch(failure(429, "read_capacity", "Too many note snapshots are in progress."));
      return;
    }
    ++readers;
    const quint64 request = activeSearch;
    auto *watcher = new QFutureWatcher<QHash<QString, QByteArray>>(owner);
    QObject::connect(watcher, &QFutureWatcher<QHash<QString, QByteArray>>::finished, owner,
                     [this, watcher, request] {
                       --readers;
                       const auto digests = watcher->result();
                       watcher->deleteLater();
                       const auto item = pending.value(request);
                       if (!item || stopped || activeSearch != request)
                         return;
                       if (!authorized(item->context)) {
                         finishSearch({});
                         return;
                       }
                       item->current.paths = {};
                       for (const auto &node : item->current.nodes) {
                         Node fresh;
                         if (!digests.contains(node.path) ||
                             resolve(node.notebook, node.id, item->context, &fresh).error.status ||
                             fresh.metadata != node.metadata || fresh.encoding != node.encoding ||
                             fresh.root != node.root)
                           continue;
                         item->current.paths.append(node.path);
                         item->current.digests.insert(node.path, digests.value(node.path));
                       }
                       if (item->current.paths.isEmpty())
                         searchStep();
                       else
                         submitSearch();
                     });
    watcher->setFuture(
        QtConcurrent::run([nodes = item->current.nodes, cancelled = item->context.cancelled] {
          QHash<QString, QByteArray> digests;
          for (const auto &node : nodes) {
            if (cancelled->load())
              break;
            const auto disk = readSaved(node.root, node.path, node.encoding, cancelled);
            if (!disk.error.error.status)
              digests.insert(node.path, disk.digest);
          }
          return digests;
        }));
  }
  void submitSearch() {
    const auto item = pending.value(activeSearch);
    if (!item || !authorized(item->context)) {
      finishSearch({});
      return;
    }
    // Never pass an empty scope: the shared core treats that as the whole notebook.
    if (item->current.paths.isEmpty()) {
      searchStep();
      return;
    }
    QJsonObject query{
        {"pattern", item->command.searchContent ? item->command.query : QStringLiteral("*")},
        {"regex", false},
        {"caseSensitive", false},
        {"wholeWord", false},
        {"maxResults", qBound(1, item->command.limit, 50) - item->items.size()},
        {"includeFiles", true},
        {"includeFolders", false},
        {QLatin1String(vxcore::kJsonKeyMatchTarget), "name"}};
    const auto queryJson = QString::fromUtf8(QJsonDocument(query).toJson(QJsonDocument::Compact));
    const auto scope = QString::fromUtf8(
        QJsonDocument(QJsonObject{{QLatin1String(vxcore::kJsonKeyFiles), item->current.paths},
                                  {QLatin1String(vxcore::kJsonKeyFolders), QJsonArray{}}})
            .toJson(QJsonDocument::Compact));
    auto *search = services.get<SearchService>();
    item->searchToken = item->command.searchContent
                            ? search->searchContent(item->current.notebook, queryJson, scope)
                            : search->searchFiles(item->current.notebook, queryJson, scope);
  }
  void searchFinished(int token, const SearchResult &result) {
    const auto item = pending.value(activeSearch);
    if (!item || item->searchToken != token)
      return;
    item->searchToken = 0;
    if (!authorized(item->context)) {
      finishSearch({});
      return;
    }
    item->truncated = item->truncated || result.m_truncated;
    QList<QPair<Node, SearchFileResult>> candidates;
    for (const auto &file : result.m_fileResults) {
      const auto id = item->current.ids.value(file.m_path);
      if (id.isEmpty() || (!file.m_id.isEmpty() && file.m_id != id))
        continue;
      Node node;
      if (resolve(item->current.notebook, id, item->context, &node).error.status ||
          node.path != file.m_path || (item->command.searchContent && !node.readable))
        continue;
      if (candidates.size() + item->items.size() >= qBound(1, item->command.limit, 50)) {
        item->truncated = true;
        break;
      }
      candidates.append({std::move(node), file});
    }
    if (!item->command.searchContent) {
      for (const auto &candidate : candidates) {
        const auto &node = candidate.first;
        item->items.append(QJsonObject{{QLatin1String(vxcore::kJsonKeyId), node.id},
                                       {QLatin1String(vxcore::kJsonKeyNotebookId), node.notebook},
                                       {"path", node.path},
                                       {QLatin1String(vxcore::kJsonKeyName), node.name},
                                       {"snippet", ""},
                                       {"view", "saved"}});
      }
      searchStep();
      return;
    }
    if (readers >= kMaxReadWorkers) {
      finishSearch(failure(429, "read_capacity", "Too many note snapshots are in progress."));
      return;
    }
    ++readers;
    const quint64 request = activeSearch;
    auto *watcher = new QFutureWatcher<QHash<QString, QString>>(owner);
    QObject::connect(watcher, &QFutureWatcher<QHash<QString, QString>>::finished, owner,
                     [this, watcher, request, candidates] {
                       --readers;
                       const auto snippets = watcher->result();
                       watcher->deleteLater();
                       const auto item = pending.value(request);
                       if (!item || stopped || request != activeSearch)
                         return;
                       if (!authorized(item->context)) {
                         finishSearch({});
                         return;
                       }
                       for (const auto &candidate : candidates) {
                         const auto &node = candidate.first;
                         Node fresh;
                         if (!snippets.contains(node.path) ||
                             resolve(node.notebook, node.id, item->context, &fresh).error.status ||
                             fresh.metadata != node.metadata || fresh.encoding != node.encoding ||
                             fresh.root != node.root)
                           continue;
                         item->items.append(
                             QJsonObject{{QLatin1String(vxcore::kJsonKeyId), node.id},
                                         {QLatin1String(vxcore::kJsonKeyNotebookId), node.notebook},
                                         {"path", node.path},
                                         {QLatin1String(vxcore::kJsonKeyName), node.name},
                                         {"snippet", snippets.value(node.path)},
                                         {"view", "saved"}});
                       }
                       searchStep();
                     });
    watcher->setFuture(QtConcurrent::run([candidates, digests = item->current.digests,
                                          cancelled = item->context.cancelled,
                                          query = item->command.query] {
      QHash<QString, QString> snippets;
      for (const auto &candidate : candidates) {
        if (cancelled->load())
          break;
        const auto &node = candidate.first;
        const auto &file = candidate.second;
        const auto disk = readSaved(node.root, node.path, node.encoding, cancelled);
        if (disk.error.error.status || disk.digest != digests.value(node.path) ||
            file.m_lineMatches.isEmpty())
          continue;
        // Validate only returned match lines against the safe saved snapshot. This is not
        // a second search engine and never trusts text read through a raced core path.
        bool valid = true;
        QString snippet;
        for (const auto &match : file.m_lineMatches) {
          qsizetype begin = 0;
          for (int line = 1; line < match.m_lineNumber && begin >= 0; ++line) {
            const auto newline = disk.text.indexOf(QLatin1Char('\n'), begin);
            begin = newline < 0 ? -1 : newline + 1;
          }
          if (match.m_lineNumber < 1 || begin < 0) {
            valid = false;
            break;
          }
          const auto newline = disk.text.indexOf(QLatin1Char('\n'), begin);
          QString savedLine = disk.text.mid(begin, newline < 0 ? -1 : newline - begin);
          if (savedLine.endsWith(QLatin1Char('\r')))
            savedLine.chop(1);
          if (savedLine != match.m_lineText || !savedLine.contains(query, Qt::CaseInsensitive)) {
            valid = false;
            break;
          }
          if (snippet.isEmpty())
            snippet = savedLine.left(512);
        }
        if (valid)
          snippets.insert(node.path, snippet);
      }
      return snippets;
    }));
  }
  void finishSearch(ApiResult result) {
    const auto id = activeSearch;
    activeSearch = 0;
    finish(id, std::move(result));
    startNextSearch();
  }
  void expire() {
    const auto ids = pending.keys();
    for (quint64 id : ids) {
      const auto item = pending.value(id);
      if (!item || (authorized(item->context) && Clock::now() < item->deadline))
        continue;
      if (item->context.cancelled)
        item->context.cancelled->store(true);
      if (item->operationToken) {
        operations.cancel(item->operationToken);
        // A commit may already be irreversible; only the domain service knows its outcome.
        continue;
      }
      if (item->searchToken)
        services.get<SearchService>()->cancel(item->searchToken);
      const auto error =
          failure(503, "deadline_exceeded", "The request deadline expired or authorization ended.");
      if (id == activeSearch) {
        // Retain the submitted slot until its terminal signal: cancellation is cooperative.
        if (item->complete) {
          auto callback = std::move(item->complete);
          callback(error);
        }
      } else {
        searchQueue.removeAll(id);
        finish(id, error);
      }
    }
  }

  void execute(ApiCommand command, ApiRequestContext context, Completion complete) {
    if (stopped) {
      complete(failure(503, "stopping", "The API is stopping."));
      return;
    }
    if (!authorized(context)) {
      complete(failure(503, "cancelled", "The request is no longer authorized."));
      return;
    }
    if (!command.notebookId.isEmpty() &&
        !context.principal.notebookIds.contains(command.notebookId)) {
      complete(unavailable());
      return;
    }
    const bool write = command.kind == ApiCommandKind::CreateNote ||
                       command.kind == ApiCommandKind::ReplaceContent ||
                       command.kind == ApiCommandKind::AppendContent ||
                       command.kind == ApiCommandKind::RenameNote;
    const QString scope = command.kind == ApiCommandKind::TrashNote ? QStringLiteral("notes:trash")
                          : write                                   ? QStringLiteral("notes:write")
                                                                    : QStringLiteral("notes:read");
    if (!context.principal.scopes.contains(scope) ||
        (command.kind == ApiCommandKind::ReadContent && command.view == ApiContentView::Live &&
         !context.principal.scopes.contains(QStringLiteral("notes:read-live")))) {
      complete(failure(403, "insufficient_scope", "The token does not permit this operation."));
      return;
    }
    if (!context.cancelled)
      context.cancelled = std::make_shared<std::atomic_bool>(false);
    const auto id = nextId++;
    auto item = std::make_shared<Pending>();
    item->command = std::move(command);
    item->context = std::move(context);
    item->complete = std::move(complete);
    pending.insert(id, item);
    const auto &cmd = item->command;
    if (cmd.kind == ApiCommandKind::ListNotebooks) {
      QJsonArray books;
      auto *core = services.get<NotebookCoreService>();
      if (!core) {
        finish(id, failure(503, "unavailable", "Notebook services are unavailable."));
        return;
      }
      auto granted = item->context.principal.notebookIds.values();
      std::sort(granted.begin(), granted.end());
      for (const auto &nb : granted) {
        const auto book = notebook(nb, item->context);
        if (book.isEmpty())
          continue;
        const bool readOnly = core->isNotebookReadOnly(nb);
        books.append(QJsonObject{
            {QLatin1String(vxcore::kJsonKeyId), nb},
            {QLatin1String(vxcore::kJsonKeyName), book.value(QLatin1String(vxcore::kJsonKeyName))},
            {QLatin1String(vxcore::kJsonKeyReadOnly), readOnly},
            {"capabilities",
             QJsonObject{{"read", true},
                         {"write", !readOnly},
                         {"trash", !readOnly && !core->getRecycleBinPath(nb).isEmpty()}}}});
      }
      ApiResult result;
      result.data = {{"items", books}};
      finish(id, result);
      return;
    }
    if (cmd.kind == ApiCommandKind::ListNodes) {
      listNodes(id);
      return;
    }
    if (cmd.kind == ApiCommandKind::Search) {
      if (searchQueue.size() + (activeSearch ? 1 : 0) >= 3) {
        finish(id, failure(429, "search_capacity", "The API search queue is full."));
        return;
      }
      prepareSearch(id);
      return;
    }
    if (cmd.kind == ApiCommandKind::CreateNote) {
      if (notebook(cmd.notebookId, item->context).isEmpty()) {
        finish(id, unavailable());
        return;
      }
      auto *core = services.get<NotebookCoreService>();
      QString parentPath;
      if (!cmd.parentId.isEmpty()) {
        Node parent;
        const auto check = resolve(cmd.notebookId, cmd.parentId, item->context, &parent, true);
        if (check.error.status || !parent.folder) {
          finish(id, check.error.status ? check : unavailable());
          return;
        }
        parentPath = parent.path;
      }
      const auto root = core->buildAbsolutePath(cmd.notebookId, QStringLiteral("."));
      if (!allowedPath(root, parentPath, true) || !ordinaryRelativePath(cmd.name) ||
          cmd.name.contains(QLatin1Char('/'))) {
        finish(id, unavailable());
        return;
      }
      auto *types = services.get<FileTypeCoreService>();
      const auto type = types ? types->getFileType(cmd.name).m_typeName : QString();
      if ((cmd.format != QLatin1String("markdown") && cmd.format != QLatin1String("text")) ||
          type != (cmd.format == QLatin1String("markdown") ? QLatin1String("Markdown")
                                                           : QLatin1String("Text"))) {
        finish(id, failure(422, "unsupported_note_type",
                           "The name must match the requested note format."));
        return;
      }
      submitOperation(id, nullptr);
      return;
    }
    Node node;
    auto check = resolve(cmd.notebookId, cmd.nodeId, item->context, &node);
    if (check.error.status) {
      finish(id, check);
      return;
    }
    if (cmd.kind == ApiCommandKind::ReadNote) {
      ApiResult result;
      result.data = node.metadata;
      finish(id, result);
      return;
    }
    if (cmd.kind == ApiCommandKind::RenameNote) {
      if (!ordinaryRelativePath(cmd.name) || cmd.name.contains(QLatin1Char('/'))) {
        finish(id, unavailable());
        return;
      }
      submitOperation(id, &node);
      return;
    }
    if (!node.readable) {
      finish(id, failure(422, "unsupported_note_type", "This note does not support text content."));
      return;
    }
    if (cmd.kind == ApiCommandKind::TrashNote && !node.trash) {
      finish(id,
             failure(422, "unsupported_trash", "This notebook does not support safe note trash."));
      return;
    }
    saved(id, node, [this, id](const Node &fresh, const DiskSnapshot &disk) {
      if (pending.value(id)->command.kind == ApiCommandKind::ReadContent)
        readContent(id, fresh, disk);
      else
        submitOperation(id, &fresh, &disk);
    });
  }
  void stop() {
    if (stopped)
      return;
    stopped = true;
    timer.stop();
    auto requests = std::move(pending);
    pending.clear();
    searchQueue.clear();
    activeSearch = 0;
    for (const auto &item : requests) {
      if (item->context.cancelled)
        item->context.cancelled->store(true);
      if (item->searchToken)
        services.get<SearchService>()->cancel(item->searchToken);
    }
    operations.shutdown();
    for (const auto &item : requests) {
      if (item->complete)
        item->complete(
            failure(503, "stopping", "The API stopped; reconcile any accepted mutation."));
    }
  }
};

VNoteApiBackend::VNoteApiBackend(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_impl(std::make_unique<Impl>(this, p_services)) {}
VNoteApiBackend::~VNoteApiBackend() { stop(); }
void VNoteApiBackend::execute(ApiCommand p_command, ApiRequestContext p_context,
                              Completion p_complete) {
  Q_ASSERT(QThread::currentThread() == thread());
  m_impl->execute(std::move(p_command), std::move(p_context), std::move(p_complete));
}
void VNoteApiBackend::stop() {
  Q_ASSERT(QThread::currentThread() == thread());
  m_impl->stop();
}
} // namespace vnotex
