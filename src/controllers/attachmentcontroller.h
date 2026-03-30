#ifndef ATTACHMENTCONTROLLER_H
#define ATTACHMENTCONTROLLER_H

#include <QModelIndex>
#include <QObject>
#include <QStringList>

namespace vnotex {

class Buffer2;
class ServiceLocator;

// Controller for attachment operations on the current buffer.
// Mediates between UI (popup) and Buffer2 API for add, open, delete,
// rename, open folder, and copy path actions.
class AttachmentController : public QObject {
  Q_OBJECT

public:
  explicit AttachmentController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Set the current buffer whose attachments are managed.
  void setBuffer(Buffer2 *p_buffer);

  // Open file dialog, copy selected files as attachments.
  void addAttachments();

  // Open attachments with the system default application.
  void openAttachments(const QStringList &p_filenames);

  // Delete attachments after confirmation dialog.
  void deleteAttachments(const QStringList &p_filenames);

  // Request inline rename on the given model index.
  void startRename(const QModelIndex &p_index);

  // Open the attachments folder in the system file manager.
  void openAttachmentsFolder();

  // Copy full paths of attachments to the system clipboard.
  void copyAttachmentPaths(const QStringList &p_filenames);

signals:
  void renameRequested(const QModelIndex &p_index);
  void attachmentAdded();
  void attachmentDeleted();

private:
  ServiceLocator &m_services;
  Buffer2 *m_buffer = nullptr;
};

} // namespace vnotex

#endif // ATTACHMENTCONTROLLER_H
