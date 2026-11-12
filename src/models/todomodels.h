#ifndef VNOTEX_TODOMODELS_H
#define VNOTEX_TODOMODELS_H

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QUuid>

#include <core/tododocument.h>

namespace vnotex {

class ServiceLocator;

enum class TodoViewKind { List, MyDay, Important, Planned, All };
enum class TodoSortOrder { Manual, Importance, DueDate, Title };

struct TodoProjection {
  TodoViewKind m_view = TodoViewKind::All;
  QString m_listKey;
  QString m_groupKey;
  QSet<QString> m_collapsedGroups;
  QString m_search;
  QString m_tag;
  TodoSortOrder m_sort = TodoSortOrder::Manual;
  QDate m_today;
  bool m_completedExpanded = false;
};

class TodoListModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    ViewKindRole,
    IncompleteCountRole,
    GroupRole,
    GroupIdRole,
    GroupTitleRole,
    ExpandedRole,
    ListCountRole
  };

  explicit TodoListModel(ServiceLocator &p_services, QObject *p_parent = nullptr);

  QModelIndex index(int p_row, int p_column = 0,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;
  QHash<int, QByteArray> roleNames() const override;

  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &p_indexes) const override;
  bool canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                       const QModelIndex &p_parent) const override;
  bool dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                    const QModelIndex &p_parent) override;
  Qt::DropActions supportedDragActions() const override;
  Qt::DropActions supportedDropActions() const override;

  void setSnapshot(const TodoDocument &p_document, const TodoProjection &p_projection,
                   bool p_editable, int p_revision);
  int getRevision() const { return m_revision; }
  void setGroupExpanded(const QString &p_key, bool p_expanded);
  int rowForKey(const QString &p_key) const;
  int rowForView(TodoViewKind p_view, const QString &p_listKey = {}) const;
  int rowForGroup(const QString &p_key) const;
  // Group headers and child lists resolve their group; smart rows return nullptr.
  const TodoGroup *groupAt(int p_row) const;
  // Smart rows have no list. Pointers remain valid until the next setSnapshot().
  const TodoList *listAt(int p_row) const;

signals:
  // Intents refer to the displayed snapshot; only the controller edits source.
  void groupMoveRequested(const QString &p_key, const QString &p_beforeGroupKey, int p_revision);
  void listMoveRequested(const QString &p_key, const QString &p_groupKey,
                         const QString &p_beforeListKey, int p_revision);

private:
  enum class DragKind : quint8 { Group, List };

  struct Drop {
    DragKind m_kind = DragKind::Group;
    QString m_key;
    QString m_groupKey;
    QString m_beforeKey;
    bool m_unchanged = false;
  };

  struct Row {
    TodoViewKind m_view;
    int m_list = -1;
    int m_group = -1;
    int m_incompleteCount = 0;
    bool m_expanded = true;
  };

  bool isOwnIndex(const QModelIndex &p_index) const;
  bool decodeDrop(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                  const QModelIndex &p_parent, Drop *p_drop) const;

  TodoDocument m_document;
  QVector<Row> m_rows;
  QUuid m_dragIdentity = QUuid::createUuid();
  quint64 m_snapshot = 0;
  int m_revision = -1;
  bool m_editable = false;
};

class TodoTaskModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    CheckedRole,
    ImportantRole,
    DueRole,
    MyDayRole,
    TagsRole,
    NoteRole,
    StepsCompletedRole,
    StepsTotalRole,
    SourceListIdRole,
    SourceListNameRole,
    SectionRole,
    ExpandedRole,
    CompletedCountRole,
    RevisionRole,
    SourceGroupIdRole,
    SourceGroupNameRole
  };

  explicit TodoTaskModel(ServiceLocator &p_services, QObject *p_parent = nullptr);

  QModelIndex index(int p_row, int p_column = 0,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex &p_index, const QVariant &p_value,
               int p_role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;
  QHash<int, QByteArray> roleNames() const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &p_indexes) const override;
  bool canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                       const QModelIndex &p_parent) const override;
  bool dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                    const QModelIndex &p_parent) override;
  Qt::DropActions supportedDragActions() const override;
  Qt::DropActions supportedDropActions() const override;

  void setSnapshot(const TodoDocument &p_document, const TodoProjection &p_projection,
                   bool p_editable, int p_revision);
  int rowForKey(const QString &p_key) const;
  // Header/out-of-range rows return nullptr. Pointers expire at setSnapshot().
  const TodoItem *itemAt(int p_row) const;
  int getActiveCount() const { return m_activeCount; }
  int getCompletedCount() const { return m_completedCount; }
  int getRevision() const { return m_revision; }
  bool isReorderingEnabled() const;

signals:
  // Intents refer to the displayed snapshot; only the controller edits source.
  void checkedChangeRequested(const QString &p_key, bool p_checked, int p_revision);
  void importanceChangeRequested(const QString &p_key, bool p_important, int p_revision);
  void moveRequested(const QString &p_key, const QString &p_listKey, const QString &p_beforeKey,
                     int p_revision);
  void completedExpansionRequested(bool p_expanded);

private:
  struct Row {
    int m_list = -1;
    int m_item = -1;
    int m_stepsCompleted = 0;
  };

  bool isOwnIndex(const QModelIndex &p_index) const;
  bool decodeDrop(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                  const QModelIndex &p_parent, QString *p_key, QString *p_beforeKey) const;

  TodoDocument m_document;
  TodoProjection m_projection;
  QVector<Row> m_rows;
  QHash<QString, int> m_rowsByKey;
  QUuid m_dragIdentity = QUuid::createUuid();
  quint64 m_snapshot = 0;
  int m_activeCount = 0;
  int m_completedCount = 0;
  int m_revision = -1;
  bool m_editable = false;
  bool m_reorderingEnabled = false;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::TodoViewKind)
Q_DECLARE_METATYPE(vnotex::TodoSortOrder)

#endif // VNOTEX_TODOMODELS_H
