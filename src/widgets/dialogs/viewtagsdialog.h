#ifndef VIEWTAGSDIALOG_H
#define VIEWTAGSDIALOG_H

#include "dialog.h"

class QLabel;

namespace vnotex {
class Node;
class TagViewer;

class ViewTagsDialog : public Dialog {
  Q_OBJECT
public:
  ViewTagsDialog(Node *p_node, QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

  void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setNode(Node *p_node);

  Node *m_node = nullptr;

  QLabel *m_nodeNameLabel = nullptr;

  TagViewer *m_tagViewer = nullptr;
};
} // namespace vnotex

#endif // VIEWTAGSDIALOG_H
