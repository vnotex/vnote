#ifndef TAGMODEL_H
#define TAGMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QJsonValue>
#include <QMap>
#include <QVector>

#include <core/servicelocator.h>

namespace vnotex {

class TagModel : public QAbstractItemModel {
  Q_OBJECT

public:
  struct TagNodeInfo {
    QString name;
    QString parent;
    int childCount = 0;
    QJsonValue metadata;
  };

  enum Roles {
    TagNameRole = Qt::UserRole + 1,
    TagParentRole,
    TagMetadataRole
  };

  explicit TagModel(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~TagModel() override;

  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_child) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;

  void setNotebookId(const QString &p_notebookId);
  QString getNotebookId() const;

  void reload();
  void reloadTag(const QString &p_tagName);

  QString tagNameFromIndex(const QModelIndex &p_index) const;
  QModelIndex indexFromTagName(const QString &p_tagName) const;

signals:
  void notebookChanged();

private:
  int childRow(const QString &p_parentTagName, const QString &p_childTagName) const;
  quintptr indexIdForTagName(const QString &p_tagName) const;
  QString tagNameForIndexId(quintptr p_indexId) const;

  ServiceLocator &m_services;
  QString m_notebookId;
  mutable QMap<QString, TagNodeInfo> m_nodeCache;
  mutable QMap<QString, QVector<QString>> m_childrenCache;
  mutable QHash<QString, quintptr> m_indexIdCache;
  mutable QHash<quintptr, QString> m_indexIdLookup;
  mutable quintptr m_nextIndexId = 1;
};

} // namespace vnotex

#endif // TAGMODEL_H
