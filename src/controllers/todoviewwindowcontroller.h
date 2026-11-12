#ifndef VNOTEX_TODOVIEWWINDOWCONTROLLER_H
#define VNOTEX_TODOVIEWWINDOWCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QTextDocument>
#include <functional>

#include <core/services/buffer2.h>
#include <core/tododocument.h>
#include <models/todomodels.h>

namespace vnotex {

class BufferService;
class ServiceLocator;

// Owns projections and source transactions, never storage or editor widgets.
class TodoViewWindowController : public QObject {
  Q_OBJECT
public:
  TodoViewWindowController(ServiceLocator &p_services, QTextDocument *p_document,
                           const Buffer2 &p_buffer, QObject *p_parent = nullptr);

  void setBuffer(const Buffer2 &p_buffer);
  void setEditable(bool p_editable);
  void refreshFromSource();
  // Source mode may defer parsing; returning to Tasks refreshes immediately.
  void setSourceRefreshEnabled(bool p_enabled);
  void setMutationGuard(std::function<bool()> p_guard);

  const TodoDocument &getDocument() const { return m_snapshot; }
  TodoListModel *getListModel() const { return m_listModel; }
  TodoTaskModel *getTaskModel() const { return m_taskModel; }
  const QString &getSelectedItemKey() const { return m_selectedItemKey; }
  void setSelectedItemKey(const QString &p_key);
  const TodoProjection &getProjection() const { return m_projection; }
  int getSnapshotRevision() const { return m_revision; }
  void setView(TodoViewKind p_view, const QString &p_listKey = {});
  void setSearch(const QString &p_search);
  void setTagFilter(const QString &p_tag);
  void setSortOrder(TodoSortOrder p_order);
  void setCompletedExpanded(bool p_expanded);
  void setToday(const QDate &p_today);

  // The caller supplies a stable owner/field key while typing. Selection, focus,
  // mode changes and non-typing gestures must end the group. Foreign source
  // changes (including undo/redo) always end it automatically.
  void setCoalescingKey(const QString &p_key);
  void endCoalescing();

  bool addList(const QString &p_title);
  bool renameList(const QString &p_listId, const QString &p_title);
  bool setListDescription(const QString &p_listId, const QString &p_markdown);
  bool removeList(const QString &p_listId);
  bool moveList(const QString &p_listId, const QString &p_beforeListId);
  bool addTask(const QString &p_listId, const TodoTaskFields &p_fields);
  bool updateTask(const QString &p_itemId, const TodoTaskFields &p_fields);
  bool removeTask(const QString &p_itemId);
  bool moveTask(const QString &p_itemId, const QString &p_listId, const QString &p_beforeItemId);
  bool addStep(const QString &p_itemId, const QString &p_title);
  bool updateStep(const QString &p_itemId, int p_index, const QString &p_title, bool p_checked);
  bool removeStep(const QString &p_itemId, int p_index);
  bool moveStep(const QString &p_itemId, int p_from, int p_to);

signals:
  void documentChanged();
  void projectionChanged();
  void selectionChanged();
  void operationRejected(const QString &p_message);

private:
  struct Change;
  bool reject(const QString &p_message);
  bool admit();
  bool snapshotIsCurrent() const;
  bool bufferIsWritable() const;
  bool commit(Change &p_change, bool p_typing = false);
  void publishSnapshot(TodoDocument p_document, const QString &p_selection,
                       const QString &p_listKey, int p_nearestRow);
  void refreshModels(int p_nearestRow = -1, bool p_preserveHiddenSelection = false);
  void ensureHeader(Change &p_change) const;
  QString ensureListIdentity(Change &p_change, const TodoList &p_list) const;
  QString ensureItemIdentity(Change &p_change, const TodoItem &p_item) const;
  void appendMetadata(Change &p_change, const QString &p_kind, const QJsonObject &p_properties,
                      const TodoSourceRange &p_range, int p_insertion,
                      const QString &p_indent = {}) const;
  bool admitModelIntent(int p_revision);

  QPointer<QTextDocument> m_document;
  BufferService *m_buffers;
  Buffer2 m_buffer;
  TodoDocument m_snapshot;
  TodoProjection m_projection;
  TodoListModel *m_listModel;
  TodoTaskModel *m_taskModel;
  QString m_selectedItemKey;
  QString m_unfilteredSelection;
  QString m_coalescingKey;
  QString m_lastCoalescingKey;
  std::function<bool()> m_mutationGuard;
  int m_revision = -1;
  int m_bufferGeneration = 0;
  bool m_editable = false;
  bool m_applying = false;
  bool m_sourceRefreshEnabled = true;
  bool m_initialized = false;
};

} // namespace vnotex

#endif // VNOTEX_TODOVIEWWINDOWCONTROLLER_H
