#ifndef ATTACHMENTPOPUP_H
#define ATTACHMENTPOPUP_H

#include "buttonpopup.h"

namespace vnotex {
class FileSystemViewer;
class Buffer;

class AttachmentPopup : public ButtonPopup {
  Q_OBJECT
public:
  AttachmentPopup(QToolButton *p_btn, QWidget *p_parent = nullptr);

  void setBuffer(Buffer *p_buffer);

private slots:
  void updateButtonsState();

private:
  void setupUI();

  QToolButton *createButton();

  bool checkRootFolderAndSingleSelection();

  void addAttachments(const QString &p_destFolderPath, const QStringList &p_files);

  void setRootFolder(const QString &p_folderPath);

  QString getDestFolderPath() const;

  void newAttachmentFile(const QString &p_destFolderPath, const QString &p_name);

  void newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name);

  void showPopupLater(const QStringList &p_pathsToSelect = QStringList());

  Buffer *m_buffer = nullptr;

  // Managed by QObject.
  FileSystemViewer *m_viewer = nullptr;

  QToolButton *m_openBtn = nullptr;
  QToolButton *m_deleteBtn = nullptr;
  QToolButton *m_copyPathBtn = nullptr;
  QToolButton *m_propertiesBtn = nullptr;

  bool m_needUpdateAttachmentFolder = true;
};
} // namespace vnotex

#endif // ATTACHMENTPOPUP_H
