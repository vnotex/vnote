#ifndef TAGVIEWER2_H
#define TAGVIEWER2_H

#include <QFrame>
#include <QHash>
#include <QIcon>
#include <QList>
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
// Designed to be embedded in a popup (TagPopup/ButtonPopup) or a dialog.
//
// Supports MULTIPLE target nodes: each tag carries a tri-state (None / Partial /
// All) describing how many of the targets currently carry it. The dialog path
// consumes addedTags()/removedTags() and applies the DELTA through the
// controller; save() is the legacy single-node overwrite kept for TagPopup2.
class TagViewer2 : public QFrame {
  Q_OBJECT

public:
  enum class TagState { None = 0, Partial = 1, All = 2 };

  explicit TagViewer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  // Load tags for a specific node. Always reloads from services.
  void setNodeId(const NodeIdentifier &p_nodeId);

  // Load tags for a set of nodes. All nodes are expected to belong to the SAME
  // notebook (the caller enforces this).
  void setNodeIds(const QList<NodeIdentifier> &p_nodeIds);

  // Tags whose state is now All but was not All originally.
  QSet<QString> addedTags() const;

  // Tags whose state is now None but was not None originally.
  QSet<QString> removedTags() const;

  // Number of target nodes whose current tags could NOT be read. Those targets
  // are excluded from the tri-state counts AND from the denominator, because a
  // failed read is indistinguishable from "no tags" and would otherwise show a
  // universally applied tag as Partial. Callers should warn when this is > 0.
  int unreadableTargetCount() const;

  // Save tag changes. Returns true if save succeeded or no changes needed.
  // LEGACY single-node overwrite path used only by TagPopup2; returns false for
  // a multi-node selection.
  bool save();

  // Check if the current notebook supports tags (bundled notebooks only).
  bool isTagSupported() const;

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

private:
  void setupUI();

  void updateTagList();

  void addTagItem(const QString &p_tagName, TagState p_state, bool p_prepend = false);

  QString itemTag(const QListWidgetItem *p_item) const;

  TagState itemTagState(const QListWidgetItem *p_item) const;

  void setItemTagState(QListWidgetItem *p_item, TagState p_state);

  void toggleItemTag(QListWidgetItem *p_item);

  QListWidgetItem *findItem(const QString &p_tagName) const;

  void filterTags(const QString &p_text);

  void handleReturnPressed();

  void initIcons();

  QString tooltipForState(const QString &p_tagName, TagState p_state) const;

  // The notebook shared by all targets (empty when there is no target).
  QString notebookId() const;

private slots:
  void refreshIcons();

private:
  ServiceLocator &m_services;

  QList<NodeIdentifier> m_nodeIds;

  // The subset of m_nodeIds whose tags were read successfully; the tri-state
  // counts and the "N of M" denominator are computed over these only.
  QList<NodeIdentifier> m_readableNodeIds;

  QLineEdit *m_searchEdit = nullptr;

  QListWidget *m_tagList = nullptr;

  // Current per-tag state.
  QHash<QString, TagState> m_tagStates;

  // Per-tag state when setNodeIds() was called (for delta detection).
  QHash<QString, TagState> m_originalStates;

  // How many target files currently carry the tag (as loaded).
  QHash<QString, int> m_tagCounts;

  QIcon m_tagIcon;

  QIcon m_selectedTagIcon;
};

} // namespace vnotex

#endif // TAGVIEWER2_H
