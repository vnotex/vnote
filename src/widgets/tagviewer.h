#ifndef TAGVIEWER_H
#define TAGVIEWER_H

#include <QFrame>

#include <functional>

#include <QIcon>
#include <QSet>
#include <QSharedPointer>

class QListWidget;
class QListWidgetItem;

namespace vnotex {
class LineEdit;
class Node;
class TagI;
class Tag;

class TagViewer : public QFrame {
  Q_OBJECT
public:
  TagViewer(bool p_isPopup, QWidget *p_parent = nullptr);

  void setNode(Node *p_node);

  void save();

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();

  void updateTagList();

  TagI *tagI();

  void addTagItem(const QString &p_tagName, bool p_selected, bool p_prepend = false);

  QString itemTag(const QListWidgetItem *p_item) const;

  bool isItemTagSelected(const QListWidgetItem *p_item) const;

  void addTags(const QSharedPointer<Tag> &p_tag, QSet<QString> &p_addedTags);

  void searchAndFilter(const QString &p_text);

  void filterItems(const std::function<bool(const QListWidgetItem *)> &p_judge);

  void handleSearchLineEditReturnPressed();

  void toggleItemTag(QListWidgetItem *p_item);

  void setItemTagSelected(QListWidgetItem *p_item, bool p_selected);

  QListWidgetItem *findItem(const QString &p_tagName) const;

  static void initIcons();

  bool m_isPopup = false;

  // View the tags of @m_node.
  Node *m_node = nullptr;

  bool m_hasChange = false;

  LineEdit *m_searchLineEdit = nullptr;

  QListWidget *m_tagList = nullptr;

  static QIcon s_tagIcon;

  static QIcon s_selectedTagIcon;
};
} // namespace vnotex

#endif // TAGVIEWER_H
