#ifndef NOTEBOOKDATABASEACCESS_H
#define NOTEBOOKDATABASEACCESS_H

#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QtSql/QSqlDatabase>

#include <core/global.h>

namespace tests {
class TestNotebookDatabase;
}

namespace vnotex {
class Node;
class Notebook;

class NotebookDatabaseAccess : public QObject {
  Q_OBJECT
public:
  enum { InvalidId = 0 };

  struct TagRecord {
    QString m_name;

    QString m_parentName;
  };

  friend class tests::TestNotebookDatabase;

  NotebookDatabaseAccess(Notebook *p_notebook, const QString &p_databaseFile,
                         QObject *p_parent = nullptr);

  bool isFresh() const;

  bool isValid() const;

  void initialize(int p_configVersion);

  bool open();

  void close();

  // Node table.
public:
  bool addNode(Node *p_node, bool p_ignoreId);

  bool addNodeRecursively(Node *p_node, bool p_ignoreId);

  // Whether there is a record with the same ID in DB and has the same path.
  bool existsNode(const Node *p_node);

  void clearObsoleteNodes();

  bool updateNode(const Node *p_node);

  bool removeNode(const Node *p_node);

  // Tag table.
public:
  // Will update the tag if exists.
  bool addTag(const QString &p_name, const QString &p_parentName);

  bool addTag(const QString &p_name);

  bool renameTag(const QString &p_name, const QString &p_newName);

  bool removeTag(const QString &p_name);

  // Sorted by parent_name.
  QList<TagRecord> getAllTags();

  QStringList queryTagAndChildren(const QString &p_tag);

  // Node_tag table.
public:
  bool updateNodeTags(Node *p_node);

  // Return the relative path of nodes of tags @p_tags.
  QStringList getNodesOfTags(const QStringList &p_tags);

private:
  struct NodeRecord {
    ID m_id = InvalidId;

    QString m_name;

    ID m_signature = InvalidId;

    ID m_parentId = InvalidId;
  };

  void setupTables(QSqlDatabase &p_db, int p_configVersion);

  QSqlDatabase getDatabase() const;

  // Return null if not exists.
  QSharedPointer<NodeRecord> queryNode(ID p_id);

  QStringList queryNodeParentPath(ID p_id);

  QString queryNodePath(ID p_id);

  bool nodeEqual(const NodeRecord *p_rec, const Node *p_node) const;

  bool existsNode(const Node *p_node, const NodeRecord *p_rec, const QStringList &p_nodePath);

  bool checkNodePath(const Node *p_node, const QStringList &p_nodePath) const;

  bool removeNode(ID p_id);

  // Return null if not exists.
  QSharedPointer<TagRecord> queryTag(const QString &p_name);

  bool updateTagParent(const QString &p_name, const QString &p_parentName);

  bool addTag(const QString &p_name, const QString &p_parentName, bool p_updateOnExists);

  QStringList queryNodeTags(ID p_id);

  QList<ID> queryTagNodes(const QString &p_tag);

  QList<ID> queryTagNodesRecursive(const QString &p_tag);

  bool removeNodeTags(ID p_id);

  bool addNodeTags(ID p_id, const QStringList &p_tags);

  Notebook *m_notebook = nullptr;

  QString m_databaseFile;

  // From Qt's docs: It is highly recommended that you do not keep a copy of the QSqlDatabase around
  // as a member of a class, as this will prevent the instance from being correctly cleaned up on
  // shutdown.
  QString m_connectionName;

  // Whether it is a new data base whether any tables.
  bool m_fresh = false;

  bool m_valid = false;

  QSet<ID> m_obsoleteNodes;
};
} // namespace vnotex

#endif // NOTEBOOKDATABASEACCESS_H
