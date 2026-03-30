#include "attachmentcontroller.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QUrl>

#include <core/services/buffer2.h>
#include <utils/clipboardutils.h>

using namespace vnotex;

AttachmentController::AttachmentController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

void AttachmentController::setBuffer(Buffer2 *p_buffer) { m_buffer = p_buffer; }

void AttachmentController::addAttachments() {
  if (!m_buffer || !m_buffer->isValid()) {
    return;
  }

  QStringList files = QFileDialog::getOpenFileNames(nullptr, tr("Add Attachments"), QString(),
                                                    tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  bool anyAdded = false;
  for (const auto &file : files) {
    QString result = m_buffer->insertAttachment(file);
    if (!result.isEmpty()) {
      anyAdded = true;
    }
  }

  if (anyAdded) {
    emit attachmentAdded();
  }
}

void AttachmentController::openAttachments(const QStringList &p_filenames) {
  if (!m_buffer || !m_buffer->isValid()) {
    return;
  }

  QString folder = m_buffer->getAttachmentsFolder();
  for (const auto &name : p_filenames) {
    QString path = folder + QLatin1Char('/') + name;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  }
}

void AttachmentController::deleteAttachments(const QStringList &p_filenames) {
  if (!m_buffer || !m_buffer->isValid() || p_filenames.isEmpty()) {
    return;
  }

  int ret = QMessageBox::question(nullptr, tr("Delete Attachments"),
                                  tr("Delete %n attachment(s)?", "", p_filenames.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ret != QMessageBox::Yes) {
    return;
  }

  bool anyDeleted = false;
  for (const auto &name : p_filenames) {
    if (m_buffer->deleteAttachment(name)) {
      anyDeleted = true;
    }
  }

  if (anyDeleted) {
    emit attachmentDeleted();
  }
}

void AttachmentController::startRename(const QModelIndex &p_index) {
  if (p_index.isValid()) {
    emit renameRequested(p_index);
  }
}

void AttachmentController::openAttachmentsFolder() {
  if (!m_buffer || !m_buffer->isValid()) {
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

  QString folder = m_buffer->getAttachmentsFolder();
  QStringList paths;
  paths.reserve(p_filenames.size());
  for (const auto &name : p_filenames) {
    paths.append(folder + QLatin1Char('/') + name);
  }

  ClipboardUtils::setTextToClipboard(paths.join(QLatin1Char('\n')));
}
