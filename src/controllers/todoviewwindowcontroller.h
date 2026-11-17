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

// Owns projections/source transactions and archive orchestration; services own storage.
class TodoViewWindowController : public QObject {
  Q_OBJECT
public:
  TodoViewWindowController(ServiceLocator &p_services, QTextDocument *p_document,
                           const Buffer2 &p_buffer, QObject *p_parent = nullptr);

  void setBuffer(const Buffer2 &p_buffer);
  void setEditable(bool p_editable);
  void refreshFromSource();
  // Suspend parsing while loading or waiting for an authoritative source snapshot.
  void setSourceRefreshEnabled(bool p_enabled);
  void setMutationGuard(std::function<bool()> p_guard);
  void setArchiveContext(quintptr p_writerKey,
                         std::function<QByteArray(const QString &)> p_encodeArchive);
  bool canArchiveCompletedTasks() const;
  bool isArchiving() const { return m_archiving; }
  bool archiveCompletedTasks();

  const TodoDocument &getDocument() const { return m_snapshot; }
  TodoListModel *getListModel() const { return m_listModel; }
  TodoTaskModel *getTaskModel() const { return m_taskModel; }
  const QString &getSelectedItemKey() const { return m_selectedItemKey; }
  void setSelectedItemKey(const QString &p_key);
  const TodoProjection &getProjection() const { return m_projection; }
  int getSnapshotRevision() const { return m_revision; }
  void setView(TodoViewKind p_view, const QString &p_listKey = {});
  void setSelectedGroupKey(const QString &p_key);
  void setGroupExpanded(const QString &p_key, bool p_expanded);
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

  bool addGroup(const QString &p_title, QString *p_createdGroupKey = nullptr);
  bool renameGroup(const QString &p_groupKey, const QString &p_title);
  bool ungroupLists(const QString &p_groupKey);
  bool moveGroup(const QString &p_groupKey, const QString &p_beforeGroupKey);
  bool addList(const QString &p_title, const QString &p_groupKey = {},
               QString *p_createdListKey = nullptr);
  bool renameList(const QString &p_listId, const QString &p_title);
  bool setListDescription(const QString &p_listId, const QString &p_markdown);
  bool setListIcon(const QString &p_listKey, const QString &p_icon);
  bool removeList(const QString &p_listId);
  bool duplicateList(const QString &p_listKey, QString *p_createdKey = nullptr);
  bool moveList(const QString &p_listId, const QString &p_groupKey, const QString &p_beforeListId);
  bool addTask(const QString &p_listId, const TodoTaskFields &p_fields);
  bool updateTask(const QString &p_itemId, const TodoTaskFields &p_fields);
  bool removeTask(const QString &p_itemId);
  bool moveTask(const QString &p_itemId, const QString &p_listId, const QString &p_beforeItemId);
  bool addStep(const QString &p_itemId, const QString &p_title);
  bool updateStep(const QString &p_itemId, int p_index, const QString &p_title, bool p_checked);
  bool removeStep(const QString &p_itemId, int p_index);
  bool promoteStep(const QString &p_itemId, int p_index);
  bool moveStep(const QString &p_itemId, int p_from, int p_to);

signals:
  void documentChanged();
  void projectionChanged();
  void selectionChanged();
  void operationRejected(const QString &p_message);
  void archiveStateChanged();
  void tasksArchived(const QString &p_path);

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
  QString ensureGroupIdentity(Change &p_change, const TodoGroup &p_group) const;
  QString ensureDefaultGroup(Change &p_change, int *p_insertion) const;
  void ensureListOwners(Change &p_change, const TodoList &p_list) const;
  QString ensureListIdentity(Change &p_change, const TodoList &p_list) const;
  QString ensureItemIdentity(Change &p_change, const TodoItem &p_item) const;
  void appendMetadata(Change &p_change, const QString &p_kind, const QJsonObject &p_properties,
                      const TodoSourceRange &p_range, int p_insertion,
                      const QString &p_indent = {}) const;
  bool admitModelIntent(int p_revision, int p_publishedRevision);

  ServiceLocator &m_services;
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
  std::function<QByteArray(const QString &)> m_encodeArchive;
  quintptr m_archiveWriterKey = 0;
  bool m_archiving = false;
  int m_revision = -1;
  int m_bufferGeneration = 0;
  bool m_editable = false;
  bool m_applying = false;
  bool m_sourceRefreshEnabled = true;
  bool m_initialized = false;
};

} // namespace vnotex

#endif // VNOTEX_TODOVIEWWINDOWCONTROLLER_H
