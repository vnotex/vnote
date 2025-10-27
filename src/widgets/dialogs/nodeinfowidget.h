#ifndef NODEINFOWIDGET_H
#define NODEINFOWIDGET_H

#include <QWidget>

#include <notebook/node.h>

class QLineEdit;
class QLabel;
class QFormLayout;
class QComboBox;

namespace vnotex {
class Notebook;
class LevelLabelWithUpButton;
class LineEditWithSnippet;

class NodeInfoWidget : public QWidget {
  Q_OBJECT
public:
  enum Mode { Create, Edit };

  NodeInfoWidget(const Node *p_node, QWidget *p_parent = nullptr);

  NodeInfoWidget(const Node *p_parentNode, Node::Flags p_flags, QWidget *p_parent = nullptr);

  QLineEdit *getNameLineEdit() const;

  QString getName() const;

  const Notebook *getNotebook() const;

  const Node *getParentNode() const;

  // Allow upper level to add more widgets to the layout.
  QFormLayout *getMainLayout() const;

  QString getFileType() const;
  void setFileType(const QString &p_typeName);

signals:
  void inputEdited();

private:
  void setupUI(const Node *p_parentNode, Node::Flags p_newNodeFlags);

  void setupFileTypeComboBox(QWidget *p_parent);

  void setupNameLineEdit(QWidget *p_parent);

  void setNode(const Node *p_node);

  Mode m_mode = Mode::Create;

  QFormLayout *m_mainLayout = nullptr;

  QComboBox *m_fileTypeComboBox = nullptr;

  LineEditWithSnippet *m_nameLineEdit = nullptr;

  LevelLabelWithUpButton *m_parentNodeLabel = nullptr;

  QLabel *m_createdDateTimeLabel = nullptr;

  QLabel *m_modifiedDateTimeLabel = nullptr;

  const Node *m_node = nullptr;

  bool m_fileTypeComboBoxMuted = false;
};
} // namespace vnotex

#endif // NODEINFOWIDGET_H
