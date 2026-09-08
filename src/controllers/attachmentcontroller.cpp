#include "attachmentcontroller.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/notebookcoreservice.h>
#include <utils/clipboardutils.h>
#include <utils/pathutils.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

AttachmentController::AttachmentController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

void AttachmentController::setBuffer(Buffer2 *p_buffer) { m_buffer = p_buffer; }

void AttachmentController::addAttachments(const QStringList &p_files) {
  if (!m_buffer || !m_buffer->isValid() || m_buffer->isReadOnly() || p_files.isEmpty()) {
    return;
  }
  const auto lease = m_buffer->isEncrypted() ? m_buffer->acquireProtectedLease() : nullptr;
  if (m_buffer->isEncrypted() && !lease) {
    emit operationFailed(tr("The protected note is locking or closed."));
    return;
  }

  bool anyAdded = false;
  for (const auto &file : p_files) {
    QString result = m_buffer->insertAttachment(file);
    if (!result.isEmpty()) {
      anyAdded = true;
    }
  }

  if (anyAdded) {
    emit attachmentAdded();
  }
}

void AttachmentController::scanAttachments(const QStringList &p_excludedPaths) {
  if (!m_buffer || !m_buffer->isValid() || !m_buffer->isAttachmentSupported() ||
      m_buffer->isReadOnly()) {
    return;
  }
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    qWarning() << "scanAttachments: notebook service unavailable";
    return;
  }
  const auto candidates = m_buffer->listUnindexedAttachments();
  if (candidates.isEmpty()) {
    return;
  }
  const auto nodeId = m_buffer->nodeId();
  const auto folder = notebookService->getAttachmentsFolder(nodeId.notebookId, nodeId.relativePath);
  if (folder.isEmpty()) {
    return;
  }
  const QDir directory(folder);
  const auto normalizedPath = [](const QString &p_path) {
    const QFileInfo info(p_path);
    if (p_path.isEmpty() || !info.isAbsolute() || !PathUtils::isLocalFile(p_path)) {
      return QString();
    }
    const auto canonical = info.canonicalFilePath();
    return PathUtils::normalizePath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
  };
  QSet<QString> excluded;
  for (const auto &path : p_excludedPaths) {
    const auto normalized = normalizedPath(path);
    if (!normalized.isEmpty()) {
      excluded.insert(normalized);
    }
  }
  for (const auto &attachment : m_buffer->listAttachments()) {
    excluded.insert(normalizedPath(directory.filePath(attachment.toString())));
  }
  excluded.insert(normalizedPath(directory.filePath(CommentService::storeFileName())));
  const auto gitkeep = normalizedPath(directory.filePath(QStringLiteral(".gitkeep")));

  bool anyAdded = false;
  for (const auto &candidate : candidates) {
    const auto name = candidate.toString();
    const auto path = directory.filePath(name);
    const auto normalized = normalizedPath(path);
    if (excluded.contains(normalized) || (normalized == gitkeep && QFileInfo(path).size() == 0)) {
      continue;
    }
    if (m_buffer->registerAttachment(name)) {
      anyAdded = true;
    }
  }
  if (anyAdded) {
    emit attachmentAdded();
  }
}

void AttachmentController::openAttachments(const QStringList &p_filenames) {
  if (!m_buffer || !m_buffer->isValid() || p_filenames.isEmpty()) {
    return;
  }

  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  if (m_buffer->isEncrypted()) {
    const auto urls = protectedResourceUrls(p_filenames);
    if (!urls.isEmpty()) {
      emit saveDecryptedCopyRequested(urls);
    }
    return;
  }

  QString folder = m_buffer->getAttachmentsFolder();
  if (folder.isEmpty()) {
    return;
  }

  for (const auto &name : p_filenames) {
    QString path = folder + QLatin1Char('/') + name;
    NodeIdentifier nodeId;
    nodeId.relativePath = path;
    bufferSvc->openBuffer(nodeId, FileOpenSettings());
  }
}

void AttachmentController::deleteAttachments(const QStringList &p_filenames) {
  if (!m_buffer || !m_buffer->isValid() || m_buffer->isReadOnly() || p_filenames.isEmpty()) {
    return;
  }

  const auto lease = m_buffer->isEncrypted() ? m_buffer->acquireProtectedLease() : nullptr;
  if (m_buffer->isEncrypted() && !lease) {
    emit operationFailed(tr("The protected note is locking or closed."));
    return;
  }
  const auto names = m_buffer->isEncrypted() ? protectedResourceUrls(p_filenames) : p_filenames;
  bool anyDeleted = false;
  for (const auto &name : names) {
    if (m_buffer->deleteAttachment(name)) {
      anyDeleted = true;
    }
  }

  if (anyDeleted) {
    emit attachmentDeleted();
  }
}

void AttachmentController::startRename(const QModelIndex &p_index) {
  if (m_buffer && m_buffer->isValid() && !m_buffer->isReadOnly() && p_index.isValid() &&
      (!m_buffer->isEncrypted() || m_buffer->acquireProtectedLease())) {
    emit renameRequested(p_index);
  }
}

void AttachmentController::openAttachmentsFolder() {
  if (!m_buffer || !m_buffer->isValid()) {
    return;
  }

  if (m_buffer->isEncrypted()) {
    VxCoreError error;
    const auto resources = m_buffer->resources(&error);
    if (error != VXCORE_OK) {
      emit operationFailed(tr("Unable to read protected attachments (%1).").arg(int(error)));
      return;
    }
    QStringList urls;
    for (const auto &value : resources) {
      const auto resource = value.toObject();
      if (resource.value(QLatin1String(vxcore::kJsonKeyRole)).toString() ==
          QLatin1String("attachment")) {
        urls.append(QStringLiteral("vxasset:") +
                    resource.value(QLatin1String(vxcore::kJsonKeyResourceId)).toString());
      }
    }
    if (!urls.isEmpty()) {
      emit saveDecryptedCopyRequested(urls);
    }
    return;
  }

  QString folder = m_buffer->getAttachmentsFolder();
  if (!folder.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
  }
}

void AttachmentController::copyAttachmentPaths(const QStringList &p_filenames) {
  if (!m_buffer || !m_buffer->isValid() || p_filenames.isEmpty()) {
    return;
  }

  if (m_buffer->isEncrypted()) {
    const auto urls = protectedResourceUrls(p_filenames);
    if (!urls.isEmpty()) {
      emit saveDecryptedCopyRequested(urls);
    }
    return;
  }

  QString folder = m_buffer->getAttachmentsFolder();
  QStringList paths;
  paths.reserve(p_filenames.size());
  for (const auto &name : p_filenames) {
    paths.append(folder + QLatin1Char('/') + name);
  }

  ClipboardUtils::setTextToClipboard(paths.join(QLatin1Char('\n')));
}

QStringList AttachmentController::protectedResourceUrls(const QStringList &p_filenames) {
  VxCoreError error;
  const auto resources = m_buffer->resources(&error);
  if (error != VXCORE_OK) {
    emit operationFailed(tr("Unable to read protected attachments (%1).").arg(int(error)));
    return {};
  }
  QStringList urls;
  for (const auto &name : p_filenames) {
    QString url;
    for (const auto &value : resources) {
      const auto resource = value.toObject();
      if (resource.value(QLatin1String(vxcore::kJsonKeyRole)).toString() ==
              QLatin1String("attachment") &&
          resource.value(QLatin1String(vxcore::kJsonKeyName)).toString() == name) {
        url = QStringLiteral("vxasset:") +
              resource.value(QLatin1String(vxcore::kJsonKeyResourceId)).toString();
        break;
      }
    }
    if (url.isEmpty()) {
      emit operationFailed(tr("The selected attachment no longer exists."));
      return {};
    }
    if (!urls.contains(url)) {
      urls.append(url);
    }
  }
  return urls;
}

QString AttachmentController::renameAttachment(const QString &p_filename,
                                               const QString &p_newName) {
  if (!m_buffer || !m_buffer->isValid() || m_buffer->isReadOnly() || p_newName.isEmpty()) {
    return {};
  }
  if (!m_buffer->isEncrypted()) {
    return m_buffer->renameAttachment(p_filename, p_newName);
  }
  const auto lease = m_buffer->acquireProtectedLease();
  if (!lease) {
    emit operationFailed(tr("The protected note is locking or closed."));
    return {};
  }
  const auto urls = protectedResourceUrls({p_filename});
  return urls.size() == 1 ? m_buffer->renameAttachment(urls.first(), p_newName) : QString();
}
