#ifndef TAGVIEWER2_H
#define TAGVIEWER2_H

#include <QFrame>
#include <QIcon>
#include <QSet>
#include <QString>

#include <core/nodeidentifier.h>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace vnotex {

class ServiceLocator;

// Tag viewer widget for the new MVC architecture.
// Displays a flat list of tags with search, toggle, and inline creation.
// Designed to be embedded in a popup (TagPopup/ButtonPopup).
class TagViewer2 : public QFrame {
  Q_OBJECT

public:
  explicit TagViewer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  // Load tags for a specific node. Always reloads from services.
  void setNodeId(const NodeIdentifier &p_nodeId);

  // Save tag changes. Returns true if save succeeded or no changes needed.
  bool save();

  // Check if the current notebook supports tags (bundled notebooks only).
  bool isTagSupported() const;

private:
  void setupUI();

  void updateTagList();

  void addTagItem(const QString &p_tagName, bool p_selected, bool p_prepend = false);

  QString itemTag(const QListWidgetItem *p_item) const;

  bool isItemTagSelected(const QListWidgetItem *p_item) const;

  void setItemTagSelected(QListWidgetItem *p_item, bool p_selected);

  void toggleItemTag(QListWidgetItem *p_item);

  QListWidgetItem *findItem(const QString &p_tagName) const;

  void filterTags(const QString &p_text);

  void handleReturnPressed();

  void initIcons();

  ServiceLocator &m_services;

  NodeIdentifier m_nodeId;

  QLineEdit *m_searchEdit = nullptr;

  QListWidget *m_tagList = nullptr;

  // Currently selected tags (toggled on).
  QSet<QString> m_selectedTags;

  // Original tags when setNodeId() was called (for change detection).
  QSet<QString> m_originalTags;

  QIcon m_tagIcon;

  QIcon m_selectedTagIcon;
};

} // namespace vnotex

#endif // TAGVIEWER2_H
