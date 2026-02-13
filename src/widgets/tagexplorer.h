#ifndef TAGEXPLORER_H
#define TAGEXPLORER_H

#include <QComboBox>
#include <QFrame>
#include <QScopedPointer>
#include <QSharedPointer>

#include "navigationmodewrapper.h"

class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;

namespace vnotex {
class TitleBar;
class Notebook;
class Tag;
class TreeWidget;

class TagExplorer : public QFrame {
  Q_OBJECT
public:
  explicit TagExplorer(QWidget *p_parent = nullptr);

  QByteArray saveState() const;

  void restoreState(const QByteArray &p_data);

public slots:
  void setNotebook(const QSharedPointer<Notebook> &p_notebook);

private slots:
  void handleNodeListContextMenuRequested(const QPoint &p_pos);

  void handleTagTreeContextMenuRequested(const QPoint &p_pos);

  void handleTagMoved(QTreeWidgetItem *p_item);

  void filterTags(const QString &p_text);

private:
  enum Column { Name = 0 };

  void initIcons();

  void setupUI();

  void setupTitleBar(QWidget *p_parent = nullptr);

  void setupTagTree(QWidget *p_parent = nullptr);

  void setupNodeList(QWidget *p_parent = nullptr);

  void setTwoColumnsEnabled(bool p_enabled);

  void updateTags();

  void loadTagChildren(const QSharedPointer<Tag> &p_tag, QTreeWidgetItem *p_parentItem);

  void fillTagItem(const QSharedPointer<Tag> &p_tag, QTreeWidgetItem *p_item) const;

  void activateTagItem();

  QString itemTag(const QTreeWidgetItem *p_item) const;

  QString itemNode(const QListWidgetItem *p_item) const;

  void updateNodeList(const QStringList &p_tags);

  void openItem(const QListWidgetItem *p_item);

  void newTag();

  void renameTag();

  void removeTag();

  void scrollToTag(const QString &p_name);

  QSharedPointer<Notebook> m_notebook;

  // Used to cache current selected tag after update.
  QString m_lastTagName;

  TitleBar *m_titleBar = nullptr;

  QSplitter *m_splitter = nullptr;

  TreeWidget *m_tagTree = nullptr;

  QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_tagTreeNavigationWrapper;

  QListWidget *m_nodeList = nullptr;

  QScopedPointer<NavigationModeWrapper<QListWidget, QListWidgetItem>> m_nodeListNavigationWrapper;

  QIcon m_tagIcon;

  QIcon m_nodeIcon;

  QComboBox *m_tagSearchEdit = nullptr;
};
} // namespace vnotex

#endif // TAGEXPLORER_H
