#include "notebookdatabaseaccess.h"

#include <QtSql>
#include <QDebug>

#include <core/exception.h>

#include "notebook.h"
#include "node.h"

using namespace vnotex;

static QString c_nodeTableName = "node";

NotebookDatabaseAccess::NotebookDatabaseAccess(Notebook *p_notebook, const QString &p_databaseFile, QObject *p_parent)
    : QObject(p_parent),
      m_notebook(p_notebook),
      m_databaseFile(p_databaseFile),
      m_connectionName(p_databaseFile)
{
}

bool NotebookDatabaseAccess::open()
{
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databaseFile);
    if (!db.open()) {
        qWarning() << QString("failed to open notebook database (%1) (%2)").arg(m_databaseFile, db.lastError().text());
        return false;
    }

    {
        // Enable foreign key support.
        QSqlQuery query(db);
        if (!query.exec("PRAGMA foreign_keys = ON")) {
            qWarning() << "failed to turn on foreign key support" << query.lastError().text();
            return false;
        }
    }

    m_valid = true;
    m_fresh = db.tables().isEmpty();
    return true;
}

bool NotebookDatabaseAccess::isFresh() const
{
    return m_fresh;
}

bool NotebookDatabaseAccess::isValid() const
{
    return m_valid;
}

// Maybe insert new table according to @p_configVersion.
void NotebookDatabaseAccess::setupTables(QSqlDatabase &p_db, int p_configVersion)
{
    Q_UNUSED(p_configVersion);

    if (!m_valid) {
        return;
    }

    QSqlQuery query(p_db);

    // Node.
    if (m_fresh) {
        bool ret = query.exec(QString("CREATE TABLE %1 (\n"
                                      "    id INTEGER PRIMARY KEY,\n"
                                      "    name text NOT NULL,\n"
                                      "    signature INTEGER NOT NULL,\n"
                                      "    parent_id INTEGER NULL REFERENCES %1(id) ON DELETE CASCADE)\n").arg(c_nodeTableName));
        if (!ret) {
            qWarning() << QString("failed to create database table (%1) (%2)").arg(c_nodeTableName, query.lastError().text());
            m_valid = false;
            return;
        }
    }
}

void NotebookDatabaseAccess::initialize(int p_configVersion)
{
    open();

    auto db = getDatabase();
    setupTables(db, p_configVersion);
}

void NotebookDatabaseAccess::close()
{
    getDatabase().close();
    QSqlDatabase::removeDatabase(m_connectionName);
    m_valid = false;
}

bool NotebookDatabaseAccess::addNode(Node *p_node, bool p_ignoreId)
{
    p_node->load();

    Q_ASSERT(p_node->getSignature() != Node::InvalidId);

    auto db = getDatabase();
    QSqlQuery query(db);
    if (p_ignoreId) {
        query.prepare(QString("INSERT INTO %1 (name, signature, parent_id)\n"
                              "    VALUES (:name, :signature, :parent_id)").arg(c_nodeTableName));
        query.bindValue(":name", p_node->getName());
        query.bindValue(":signature", p_node->getSignature());
        query.bindValue(":parent_id", p_node->getParent() ? p_node->getParent()->getId() : QVariant());
    } else {
        bool useNewId = false;
        if (p_node->getId() != InvalidId) {
            auto nodeRec = queryNode(p_node->getId());
            if (nodeRec) {
                auto nodePath = queryNodePath(p_node->getId());
                if (existsNode(p_node, nodeRec.data(), nodePath)) {
                    return true;
                }

                if (nodePath.isEmpty()) {
                    useNewId = true;
                    m_obsoleteNodes.insert(nodeRec->m_id);
                } else {
                    auto relativePath = nodePath.join(QLatin1Char('/'));
                    auto oldNode = m_notebook->loadNodeByPath(relativePath);
                    Q_ASSERT(oldNode != p_node);
                    if (oldNode) {
                        // The node with the same id still exists.
                        useNewId = true;
                    } else if (nodeRec->m_signature == p_node->getSignature() && nodeRec->m_name == p_node->getName()) {
                        // @p_node should be the same node as @nodeRec.
                        return updateNode(p_node);
                    } else {
                        // @nodeRec is now an obsolete node.
                        useNewId = true;
                        m_obsoleteNodes.insert(nodeRec->m_id);
                    }
                }
            }
        } else {
            useNewId = true;
        }

        if (useNewId) {
            query.prepare(QString("INSERT INTO %1 (name, signature, parent_id)\n"
                                  "    VALUES (:name, :signature, :parent_id)").arg(c_nodeTableName));
        } else {
            query.prepare(QString("INSERT INTO %1 (id, name, signature, parent_id)\n"
                                  "    VALUES (:id, :name, :signature, :parent_id)").arg(c_nodeTableName));
            query.bindValue(":id", p_node->getId());
        }
        query.bindValue(":name", p_node->getName());
        query.bindValue(":signature", p_node->getSignature());
        query.bindValue(":parent_id", p_node->getParent() ? p_node->getParent()->getId() : QVariant());
    }

    if (!query.exec()) {
        qWarning() << "failed to add node by query" << query.executedQuery() << query.lastError().text();
        return false;
    }

    const ID id = query.lastInsertId().toULongLong();
    const ID preId = p_node->getId();
    p_node->updateId(id);

    qDebug("added node id %llu preId %llu ignoreId %d sig %llu name %s parentId %llu",
           id,
           preId,
           p_ignoreId,
           p_node->getSignature(),
           p_node->getName().toStdString(),
           p_node->getParent() ? p_node->getParent()->getId() : Node::InvalidId);
    return true;
}

QSharedPointer<NotebookDatabaseAccess::NodeRecord> NotebookDatabaseAccess::queryNode(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT id, name, signature, parent_id from %1 where id = :id").arg(c_nodeTableName));
    query.bindValue(":id", p_id);
    if (!query.exec()) {
        qWarning() << "failed to query node" << query.executedQuery() << query.lastError().text();
        return nullptr;
    }

    if (query.next()) {
        auto nodeRec = QSharedPointer<NodeRecord>::create();
        nodeRec->m_id = query.value(0).toULongLong();
        nodeRec->m_name = query.value(1).toString();
        nodeRec->m_signature = query.value(2).toULongLong();
        nodeRec->m_parentId = query.value(3).toULongLong();
        return nodeRec;
    }

    return nullptr;
}

QSqlDatabase NotebookDatabaseAccess::getDatabase() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool NotebookDatabaseAccess::existsNode(const Node *p_node)
{
    if (!p_node) {
        return false;
    }

    return existsNode(p_node,
                      queryNode(p_node->getId()).data(),
                      queryNodePath(p_node->getId()));
}

bool NotebookDatabaseAccess::existsNode(const Node *p_node, const NodeRecord *p_rec, const QStringList &p_nodePath)
{
    if (p_nodePath.isEmpty()) {
        return false;
    }

    if (!nodeEqual(p_rec, p_node)) {
        return false;
    }

    return checkNodePath(p_node, p_nodePath);
}

QStringList NotebookDatabaseAccess::queryNodePath(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("WITH RECURSIVE cte_parents(id, name, parent_id) AS (\n"
                          "    SELECT node.id, node.name, node.parent_id\n"
                          "    FROM %1 node\n"
                          "    WHERE node.id = :id\n"
                          "    UNION ALL\n"
                          "    SELECT node.id, node.name, node.parent_id\n"
                          "    FROM %1 node\n"
                          "    JOIN cte_parents cte ON node.id = cte.parent_id\n"
                          "    LIMIT 5000)\n"
                          "SELECT * FROM cte_parents").arg(c_nodeTableName));
    query.bindValue(":id", p_id);
    if (!query.exec()) {
        qWarning() << "failed to query node's path" << query.executedQuery() << query.lastError().text();
        return QStringList();
    }

    QStringList ret;
    ID lastParentId = p_id;
    bool hasResult = false;
    while (query.next()) {
        hasResult = true;
        Q_ASSERT(lastParentId == query.value(0).toULongLong());
        ret.prepend(query.value(1).toString());
        lastParentId = query.value(2).toULongLong();
    }
    Q_ASSERT(!hasResult || lastParentId == InvalidId);
    return ret;
}

bool NotebookDatabaseAccess::updateNode(const Node *p_node)
{
    Q_ASSERT(p_node->getParent());

    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("UPDATE %1\n"
                          "SET name = :name,\n"
                          "    signature = :signature,\n"
                          "    parent_id = :parent_id\n"
                          "WHERE id = :id").arg(c_nodeTableName));
    query.bindValue(":name", p_node->getName());
    query.bindValue(":signature", p_node->getSignature());
    query.bindValue(":parent_id", p_node->getParent()->getId());
    query.bindValue(":id", p_node->getId());
    if (!query.exec()) {
        qWarning() << "failed to update node" << query.executedQuery() << query.lastError().text();
        return false;
    }

    qDebug() << "updated node"
             << p_node->getId()
             << p_node->getSignature()
             << p_node->getName()
             << p_node->getParent()->getId();

    return true;
}

void NotebookDatabaseAccess::clearObsoleteNodes()
{
    if (m_obsoleteNodes.isEmpty()) {
        return;
    }

    for (auto it : m_obsoleteNodes) {
        if (!removeNode(it)) {
            qWarning() << "failed to clear obsolete node" << it;
            continue;
        }
    }

    m_obsoleteNodes.clear();
}

bool NotebookDatabaseAccess::removeNode(const Node *p_node)
{
    if (existsNode(p_node)) {
        return removeNode(p_node->getId());
    }

    return true;
}

bool NotebookDatabaseAccess::removeNode(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("DELETE FROM %1\n"
                          "WHERE id = :id").arg(c_nodeTableName));
    query.bindValue(":id", p_id);
    if (!query.exec()) {
        qWarning() << "failed to remove node" << query.executedQuery() << query.lastError().text();
        return false;
    }
    qDebug() << "removed node" << p_id;
    return true;
}

bool NotebookDatabaseAccess::nodeEqual(const NodeRecord *p_rec, const Node *p_node) const
{
    if (!p_rec) {
        if (p_node) {
            return false;
        } else {
            return true;
        }
    } else if (!p_node) {
        return false;
    }

    if (p_rec->m_id != p_node->getId()) {
        return false;
    }
    if (p_rec->m_name != p_node->getName()) {
        return false;
    }
    if (p_rec->m_signature != p_node->getSignature()) {
        return false;
    }
    if (p_node->getParent()) {
        if (p_rec->m_parentId != p_node->getParent()->getId()) {
            return false;
        }
    } else if (p_rec->m_parentId != Node::InvalidId) {
        return false;
    }

    return true;
}

bool NotebookDatabaseAccess::checkNodePath(const Node *p_node, const QStringList &p_nodePath) const
{
    for (int i = p_nodePath.size() - 1; i >= 0; --i) {
        if (!p_node) {
            return false;
        }

        if (p_nodePath[i] != p_node->getName()) {
            return false;
        }
        p_node = p_node->getParent();
    }

    if (p_node) {
        return false;
    }

    return true;
}
