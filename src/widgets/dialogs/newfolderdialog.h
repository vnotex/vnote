#ifndef NEWFOLDERDIALOG_H
#define NEWFOLDERDIALOG_H

#include "scrolldialog.h"

namespace vnotex {
class Node;
class NodeInfoWidget;

class NewFolderDialog : public ScrollDialog {
  Q_OBJECT
public:
  // New a folder under @p_node.
  NewFolderDialog(Node *p_node, QWidget *p_parent = nullptr);

  const QSharedPointer<Node> &getNewNode() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI(const Node *p_node);

  void setupNodeInfoWidget(const Node *p_node, QWidget *p_parent);

  bool validateNameInput(QString &p_msg);

  bool newFolder();

  bool validateInputs();

  NodeInfoWidget *m_infoWidget = nullptr;

  QSharedPointer<Node> m_newNode;
};
} // namespace vnotex

#endif // NEWFOLDERDIALOG_H
