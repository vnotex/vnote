#ifndef ATTACHMENTPOPUP2_H
#define ATTACHMENTPOPUP2_H

#include "buttonpopup.h"

class QLabel;
class QToolButton;
class QListView;

namespace vnotex {

class ServiceLocator;
class Buffer2;
class AttachmentListModel;
class AttachmentController;

class AttachmentPopup2 : public ButtonPopup {
  Q_OBJECT

public:
  AttachmentPopup2(ServiceLocator &p_services, QToolButton *p_btn,
                   QWidget *p_parent = nullptr);

  void setBuffer(Buffer2 *p_buffer);

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();
  void updateButtonsState();

  QStringList getSelectedFilenames() const;

  ServiceLocator &m_services;
  Buffer2 *m_buffer = nullptr;

  // Managed by QObject.
  AttachmentListModel *m_model = nullptr;
  AttachmentController *m_controller = nullptr;
  QListView *m_listView = nullptr;
  QLabel *m_unsupportedLabel = nullptr;
  QLabel *m_countLabel = nullptr;

  // Buttons — managed by QObject.
  QToolButton *m_addBtn = nullptr;
  QToolButton *m_openBtn = nullptr;
  QToolButton *m_deleteBtn = nullptr;
  QToolButton *m_renameBtn = nullptr;
  QToolButton *m_openFolderBtn = nullptr;
  QToolButton *m_copyPathBtn = nullptr;
};

} // namespace vnotex

#endif // ATTACHMENTPOPUP2_H
