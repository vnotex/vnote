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

  // Copy the given files into the buffer's attachments folder.
  // The caller (view) owns the file dialog; this never shows UI.
  void addAttachments(const QStringList &p_files);

  // Open attachments with the system default application.
  void openAttachments(const QStringList &p_filenames);

  // Delete the given attachments.
  // The caller (view) owns the confirmation dialog; this never shows UI.
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
