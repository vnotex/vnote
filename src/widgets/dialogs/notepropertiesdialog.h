#ifndef NOTEPROPERTIESDIALOG_H
#define NOTEPROPERTIESDIALOG_H

#include "scrolldialog.h"

namespace vnotex {
class Node;
class NodeInfoWidget;

class NotePropertiesDialog : public ScrollDialog {
  Q_OBJECT
public:
  NotePropertiesDialog(Node *p_node, QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupNodeInfoWidget(QWidget *p_parent);

  bool validateNameInput(QString &p_msg);

  bool saveNoteProperties();

  bool validateInputs();

  NodeInfoWidget *m_infoWidget = nullptr;

  Node *m_node = nullptr;
};
} // namespace vnotex

#endif // NOTEPROPERTIESDIALOG_H
