#include "notebookdatabaseaccess.h"

#include <QtSql>
#include <QDebug>
#include <QSet>

#include <core/exception.h>

#include "notebook.h"
#include "node.h"

using namespace vnotex;

static QString c_nodeTableName = "node";

static QString c_tagTableName = "tag";

static QString c_nodeTagTableName = "tag_node";

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

    if (m_fresh) {
        // Node.
        bool ret = query.exec(QString("CREATE TABLE %1 (\n"
                                      "    id INTEGER PRIMARY KEY,\n"
                                      "    name TEXT NOT NULL,\n"
                                      "    signature INTEGER NOT NULL,\n"
                                      "    parent_id INTEGER NULL REFERENCES %1(id) ON DELETE CASCADE ON UPDATE CASCADE)\n").arg(c_nodeTableName));
        if (!ret) {
            qWarning() << QString("failed to create database table (%1) (%2)").arg(c_nodeTableName, query.lastError().text());
            m_valid = false;
            return;
        }

        // Tag.
        ret = query.exec(QString("CREATE TABLE %1 (\n"
                                 "    name TEXT PRIMARY KEY,\n"
                                 "    parent_name TEXT NULL REFERENCES %1(name) ON DELETE CASCADE ON UPDATE CASCADE) WITHOUT ROWID\n").arg(c_tagTableName));
        if (!ret) {
            qWarning() << QString("failed to create database table (%1) (%2)").arg(c_tagTableName, query.lastError().text());
            m_valid = false;
            return;
        }

        // Node_Tag.
        ret = query.exec(QString("CREATE TABLE %1 (\n"
                                 "    node_id INTEGER REFERENCES %2(id) ON DELETE CASCADE ON UPDATE CASCADE,\n"
                                 "    tag_name TEXT REFERENCES %3(name) ON DELETE CASCADE ON UPDATE CASCADE)\n").arg(c_nodeTagTableName,
                                                                                                                     c_nodeTableName,
                                                                                                                     c_tagTableName));
        if (!ret) {
            qWarning() << QString("failed to create database table (%1) (%2)").arg(c_nodeTagTableName, query.lastError().text());
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
                auto nodePath = queryNodeParentPath(p_node->getId());
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
        qWarning() << "failed to add node" << query.executedQuery() << query.lastError().text();
        return false;
    }

    const ID id = query.lastInsertId().toULongLong();
    p_node->updateId(id);

    qDebug() << "added node id" << id << p_node->getName();
    return true;
}

bool NotebookDatabaseAccess::addNodeRecursively(Node *p_node, bool p_ignoreId)
{
    if (!p_node) {
        return false;
    }

    auto paNode = p_node->getParent();
    if (paNode && !addNodeRecursively(paNode, p_ignoreId)) {
        return false;
    }

    return addNode(p_node, p_ignoreId);
}

QSharedPointer<NotebookDatabaseAccess::NodeRecord> NotebookDatabaseAccess::queryNode(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT id, name, signature, parent_id FROM %1 WHERE id = :id").arg(c_nodeTableName));
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
                      queryNodeParentPath(p_node->getId()));
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

QStringList NotebookDatabaseAccess::queryNodeParentPath(ID p_id)
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
                          "SELECT id, name, parent_id FROM cte_parents").arg(c_nodeTableName));
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

QString NotebookDatabaseAccess::queryNodePath(ID p_id)
{
    auto parentPath = queryNodeParentPath(p_id);
    if (parentPath.isEmpty()) {
        return QString();
    }

    if (parentPath.size() == 1) {
        return parentPath.first();
    }

    QString relativePath = parentPath.join(QLatin1Char('/'));
    Q_ASSERT(relativePath[0] == QLatin1Char('/'));
    return relativePath.mid(1);
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

bool NotebookDatabaseAccess::addTag(const QString &p_name, const QString &p_parentName)
{
    return addTag(p_name, p_parentName, true);
}

bool NotebookDatabaseAccess::addTag(const QString &p_name)
{
    return addTag(p_name, QString(), false);
}

bool NotebookDatabaseAccess::addTag(const QString &p_name, const QString &p_parentName, bool p_updateOnExists)
{
    {
        auto tagRec = queryTag(p_name);
        if (tagRec) {
            if (!p_updateOnExists || tagRec->m_parentName == p_parentName) {
                return true;
            }

            return updateTagParent(p_name, p_parentName);
        }
    }

    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO %1 (name, parent_name)\n"
                          "    VALUES (:name, :parent_name)").arg(c_tagTableName));
    query.bindValue(":name", p_name);
    query.bindValue(":parent_name", p_parentName.isEmpty() ? QVariant() : p_parentName);

    if (!query.exec()) {
        qWarning() << "failed to add tag" << query.executedQuery() << query.lastError().text();
        return false;
    }

    qDebug() << "added tag" << p_name << "parentName" << p_parentName;
    return true;
}

QSharedPointer<NotebookDatabaseAccess::TagRecord> NotebookDatabaseAccess::queryTag(const QString &p_name)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT name, parent_name FROM %1 WHERE name = :name").arg(c_tagTableName));
    query.bindValue(":name", p_name);
    if (!query.exec()) {
        qWarning() << "failed to query tag" << query.executedQuery() << query.lastError().text();
        return nullptr;
    }

    if (query.next()) {
        auto tagRec = QSharedPointer<TagRecord>::create();
        tagRec->m_name = query.value(0).toString();
        tagRec->m_parentName = query.value(1).toString();
        return tagRec;
    }

    return nullptr;
}

bool NotebookDatabaseAccess::updateTagParent(const QString &p_name, const QString &p_parentName)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("UPDATE %1\n"
                          "SET parent_name = :parent_name\n"
                          "WHERE name = :name").arg(c_tagTableName));
    query.bindValue(":name", p_name);
    query.bindValue(":parent_name", p_parentName.isEmpty() ? QVariant() : p_parentName);
    if (!query.exec()) {
        qWarning() << "failed to update tag" << query.executedQuery() << query.lastError().text();
        return false;
    }

    qDebug() << "updated tag parent" << p_name << p_parentName;

    return true;
}

bool NotebookDatabaseAccess::renameTag(const QString &p_name, const QString &p_newName)
{
    Q_ASSERT(!p_newName.isEmpty());
    if (p_name == p_newName) {
        return true;
    }

    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("UPDATE %1\n"
                          "SET name = :new_name\n"
                          "WHERE name = :name").arg(c_tagTableName));
    query.bindValue(":name", p_name);
    query.bindValue(":new_name", p_newName);
    if (!query.exec()) {
        qWarning() << "failed to update tag" << query.executedQuery() << query.lastError().text();
        return false;
    }

    qDebug() << "updated tag name" << p_name << p_newName;

    return true;
}

bool NotebookDatabaseAccess::removeTag(const QString &p_name)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("DELETE FROM %1\n"
                          "WHERE name = :name").arg(c_tagTableName));
    query.bindValue(":name", p_name);
    if (!query.exec()) {
        qWarning() << "failed to remove tag" << query.executedQuery() << query.lastError().text();
        return false;
    }
    qDebug() << "removed tag" << p_name;
    return true;
}

bool NotebookDatabaseAccess::updateNodeTags(Node *p_node)
{
    p_node->load();

    if (p_node->getId() == Node::InvalidId) {
        qWarning() << "failed to update tags of node with invalid id" << p_node->fetchPath();
        return false;
    }

    const auto &nodeTags = p_node->getTags();

    {
        QStringList tagsList = queryNodeTags(p_node->getId());
        QSet<QString> tags;
        for (const auto &s : tagsList)
        {
            tags.insert(s);
        }
        if (tags.isEmpty() && nodeTags.isEmpty()) {
            return true;
        }

        bool needUpdate = false;
        if (tags.size() != nodeTags.size()) {
            needUpdate = true;
        }

        for (const auto &tag : nodeTags) {
            if (tags.find(tag) == tags.end()) {
                needUpdate = true;

                if (!addTag(tag)) {
                    qWarning() << "failed to add tag before addNodeTags" << p_node->getId() << tag;
                    return false;
                }
            }
        }

        if (!needUpdate) {
            return true;
        }
    }

    bool ret = removeNodeTags(p_node->getId());
    if (!ret) {
        return false;
    }

    return addNodeTags(p_node->getId(), nodeTags);
}

QStringList NotebookDatabaseAccess::queryNodeTags(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT tag_name FROM %1 WHERE node_id = :node_id").arg(c_nodeTagTableName));
    query.bindValue(":node_id", p_id);
    if (!query.exec()) {
        qWarning() << "failed to query node's tags" << query.executedQuery() << query.lastError().text();
        return QStringList();
    }

    QStringList tags;
    while (query.next()) {
        tags.append(query.value(0).toString());
    }
    return tags;
}

bool NotebookDatabaseAccess::removeNodeTags(ID p_id)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("DELETE FROM %1\n"
                          "WHERE node_id = :node_id").arg(c_nodeTagTableName));
    query.bindValue(":node_id", p_id);
    if (!query.exec()) {
        qWarning() << "failed to remove tags of node" << query.executedQuery() << query.lastError().text();
        return false;
    }
    qDebug() << "removed tags of node" << p_id;
    return true;
}

bool NotebookDatabaseAccess::addNodeTags(ID p_id, const QStringList &p_tags)
{
    Q_ASSERT(p_id != Node::InvalidId);
    if (p_tags.isEmpty()) {
        return true;
    }

    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO %1 (node_id, tag_name)\n"
                          "    VALUES (?, ?)").arg(c_nodeTagTableName));

    QVariantList ids;
    QVariantList tagNames;
    for (const auto &tag : p_tags) {
        ids << p_id;
        tagNames << tag;
    }

    query.addBindValue(ids);
    query.addBindValue(tagNames);

    if (!query.execBatch()) {
        qWarning() << "failed to add tags of node" << query.executedQuery() << query.lastError().text();
        return false;
    }

    qDebug() << "added tags of node" << p_id << p_tags;
    return true;
}

QList<ID> NotebookDatabaseAccess::queryTagNodes(const QString &p_tag)
{
    QList<ID> nodes;
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT node_id FROM %1 WHERE tag_name = :tag_name").arg(c_nodeTagTableName));
    query.bindValue(":tag_name", p_tag);
    if (!query.exec()) {
        qWarning() << "failed to query nodes of tag" << query.executedQuery() << query.lastError().text();
        return nodes;
    }

    while (query.next()) {
        nodes.append(query.value(0).toULongLong());
    }
    return nodes;
}

QList<ID> NotebookDatabaseAccess::queryTagNodesRecursive(const QString &p_tag)
{
    auto tags = queryTagAndChildren(p_tag);
    if (tags.size() <= 1) {
        return queryTagNodes(p_tag);
    }

    QSet<ID> allIds;
    for (const auto &tag : tags) {
        auto ids = queryTagNodes(tag);
        for (const auto &id : ids) {
            allIds.insert(id);
        }
    }

    return allIds.values();
}

QStringList NotebookDatabaseAccess::queryTagAndChildren(const QString &p_tag)
{
    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("WITH RECURSIVE cte_children(name, parent_name) AS (\n"
                          "    SELECT tag.name, tag.parent_name\n"
                          "    FROM %1 tag\n"
                          "    WHERE tag.name = :name\n"
                          "    UNION ALL\n"
                          "    SELECT tag.name, tag.parent_name\n"
                          "    FROM %1 tag\n"
                          "    JOIN cte_children cte ON tag.parent_name = cte.name\n"
                          "    LIMIT 5000)\n"
                          "SELECT name FROM cte_children").arg(c_tagTableName));
    query.bindValue(":name", p_tag);
    if (!query.exec()) {
        qWarning() << "failed to query tag and its children" << query.executedQuery() << query.lastError().text();
        return QStringList();
    }

    QStringList ret;
    while (query.next()) {
        ret.append(query.value(0).toString());
    }

    qDebug() << "tag and its children" << p_tag << ret;
    return ret;
}

QStringList NotebookDatabaseAccess::getNodesOfTags(const QStringList &p_tags)
{
    QStringList ret;
    if (p_tags.isEmpty()) {
        return ret;
    }

    QList<ID> nodeIds;

    if (p_tags.size() == 1) {
        nodeIds = queryTagNodesRecursive(p_tags.first());
    } else {
        QSet<ID> allIds;
        for (const auto &tag : p_tags) {
            auto ids = queryTagNodesRecursive(tag);
            for (const auto &id : ids) {
                allIds.insert(id);
            }
        }
        nodeIds = allIds.values();
    }

    for (const auto &id : nodeIds) {
        auto nodePath = queryNodePath(id);
        if (nodePath.isNull()) {
            continue;
        }

        ret.append(nodePath);
    }

    return ret;
}

QList<NotebookDatabaseAccess::TagRecord> NotebookDatabaseAccess::getAllTags()
{
    QList<TagRecord> ret;

    auto db = getDatabase();
    QSqlQuery query(db);
    query.prepare(QString("SELECT name, parent_name FROM %1 ORDER BY parent_name, name").arg(c_tagTableName));
    if (!query.exec()) {
        qWarning() << "failed to query tags" << query.executedQuery() << query.lastError().text();
        return ret;
    }

    while (query.next()) {
        ret.append(TagRecord());
        ret.last().m_name = query.value(0).toString();
        ret.last().m_parentName = query.value(1).toString();
    }
    return ret;
}
