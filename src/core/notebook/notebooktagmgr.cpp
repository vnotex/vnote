#include "notebooktagmgr.h"

#include <QDebug>
#include <QHash>

#include "bundlenotebook.h"
#include "tag.h"

using namespace vnotex;

NotebookTagMgr::NotebookTagMgr(BundleNotebook *p_notebook)
    : QObject(p_notebook),
      m_notebook(p_notebook)
{
    update();
}

QVector<NotebookTagMgr::TagGraphPair> NotebookTagMgr::stringToTagGraph(const QString &p_text)
{
    // parent>chlid;parent2>chlid2.
    QVector<TagGraphPair> tagGraph;
    auto pairs = p_text.split(QLatin1Char(';'));
    for (const auto &pa : pairs) {
        if (pa.isEmpty()) {
            continue;
        }

        auto paCh = pa.split(QLatin1Char('>'));
        if (paCh.size() != 2 || paCh[0].isEmpty() || paCh[1].isEmpty()) {
            qWarning() << "ignore invalid <parent, child> tag pair" << pa;
            continue;
        }

        TagGraphPair tagPair;
        tagPair.m_parent = paCh[0];
        tagPair.m_child = paCh[1];
        tagGraph.push_back(tagPair);
    }

    return tagGraph;
}

QString NotebookTagMgr::tagGraphToString(const QVector<TagGraphPair> &p_tagGraph)
{
    QString text;
    if (p_tagGraph.isEmpty()) {
        return text;
    }

    text = p_tagGraph[0].m_parent + QLatin1Char('>') + p_tagGraph[0].m_child;
    for (int i = 1; i < p_tagGraph.size(); ++i) {
        text += QLatin1Char(';') + p_tagGraph[i].m_parent + QLatin1Char('>') + p_tagGraph[i].m_child;
    }

    return text;
}

const QVector<QSharedPointer<Tag>> &NotebookTagMgr::getTopLevelTags() const
{
    return m_topLevelTags;
}

void NotebookTagMgr::update()
{
    auto db = m_notebook->getDatabaseAccess();
    const auto allTags = db->getAllTags();

    update(allTags);
}

void NotebookTagMgr::update(const QList<NotebookDatabaseAccess::TagRecord> &p_allTags)
{
    m_topLevelTags.clear();

    QHash<QString, Tag *> nameToTag;

    QVector<int> todoIdx;
    todoIdx.reserve(p_allTags.size());
    for (int i = 0; i < p_allTags.size(); ++i) {
        todoIdx.push_back(i);
    }

    while (!todoIdx.isEmpty()) {
        QVector<int> pendingIdx;
        pendingIdx.reserve(p_allTags.size());

        for (int i = 0; i < todoIdx.size(); ++i) {
            const auto &rec = p_allTags[todoIdx[i]];
            Q_ASSERT(!nameToTag.contains(rec.m_name));
            QSharedPointer<Tag> newTag;
            if (rec.m_parentName.isEmpty()) {
                // Top level.
                newTag = QSharedPointer<Tag>::create(rec.m_name);
                m_topLevelTags.push_back(newTag);
            } else {
                auto parentIt = nameToTag.find(rec.m_parentName);
                if (parentIt == nameToTag.end()) {
                    // Need to process its parent first.
                    pendingIdx.push_back(todoIdx[i]);
                    continue;
                } else {
                    newTag = QSharedPointer<Tag>::create(rec.m_name);
                    parentIt.value()->addChild(newTag);
                }
            }

            nameToTag.insert(newTag->name(), newTag.data());
        }

        if (todoIdx.size() == pendingIdx.size()) {
            qWarning() << "cyclic parent-chlid tag definition detected";
            break;
        }

        todoIdx = pendingIdx;
    }
}

QStringList NotebookTagMgr::findNodesOfTag(const QString &p_name)
{
    auto db = m_notebook->getDatabaseAccess();
    return db->getNodesOfTags(QStringList(p_name));
}

QSharedPointer<Tag> NotebookTagMgr::findTag(const QString &p_name)
{
    QSharedPointer<Tag> tag;
    forEachTag([&tag, p_name](const QSharedPointer<Tag> &p_tag) {
                if (p_tag->name() == p_name) {
                    tag = p_tag;
                    return false;
                }
                return true;
            });

    return tag;
}

void NotebookTagMgr::forEachTag(const TagFinder &p_func) const
{
    for (const auto &tag : m_topLevelTags) {
        if (!forEachTag(tag, p_func)) {
            return;
        }
    }
}

bool NotebookTagMgr::forEachTag(const QSharedPointer<Tag> &p_tag, const TagFinder &p_func) const
{
    if (!p_func(p_tag)) {
        return false;
    }

    for (const auto &child : p_tag->getChildren()) {
        if (!forEachTag(child, p_func)) {
            return false;
        }
    }

    return true;
}

bool NotebookTagMgr::newTag(const QString &p_name, const QString &p_parentName)
{
    if (p_name.isEmpty()) {
        return false;
    }

    auto db = m_notebook->getDatabaseAccess();
    bool ret = db->addTag(p_name, p_parentName);
    if (ret) {
        const auto allTags = db->getAllTags();
        update(allTags);
        if (!p_parentName.isEmpty()) {
            updateNotebookTagGraph(allTags);
        }
        emit m_notebook->tagsUpdated();
        return true;
    } else {
        qWarning() << "failed to new tag" << p_name << p_parentName;
        return false;
    }
}

bool NotebookTagMgr::updateNodeTags(Node *p_node)
{
    auto db = m_notebook->getDatabaseAccess();

    // Make sure the node exists in DB.
    if (!db->addNodeRecursively(p_node, false)) {
        qWarning() << "failed to add node to DB" << p_node->fetchPath() << p_node->getId() << (p_node->getParent() ? p_node->getParent()->getId() : -1);
        return false;
    }

    if (db->updateNodeTags(p_node)) {
        update();
        emit m_notebook->tagsUpdated();
        return true;
    }

    return false;
}

bool NotebookTagMgr::updateNodeTags(Node *p_node, const QStringList &p_newTags)
{
    p_node->updateTags(p_newTags);
    return updateNodeTags(p_node);
}

bool NotebookTagMgr::renameTag(const QString &p_name, const QString &p_newName)
{
    const auto nodePaths = findNodesOfTag(p_name);

    auto db = m_notebook->getDatabaseAccess();
    if (!db->renameTag(p_name, p_newName)) {
        return false;
    }

    const auto allTags = db->getAllTags();
    update(allTags);

    updateNotebookTagGraph(allTags);

    // Update node tag.
    for (const auto &pa : nodePaths) {
        auto node = m_notebook->loadNodeByPath(pa);
        if (!node) {
            qWarning() << "node belongs to tag in DB but not exists" << p_name << pa;
            continue;
        }

        auto tags = node->getTags();
        for (auto &tag : tags) {
            if (tag == p_name) {
                tag = p_newName;
                break;
            }
        }
        node->updateTags(tags);
    }

    emit m_notebook->tagsUpdated();
    return true;
}

void NotebookTagMgr::updateNotebookTagGraph(const QList<NotebookDatabaseAccess::TagRecord> &p_allTags)
{
    QVector<TagGraphPair> graph;
    graph.reserve(p_allTags.size());
    for (const auto &tag : p_allTags) {
        if (tag.m_parentName.isEmpty()) {
            continue;
        }
        TagGraphPair pa;
        pa.m_parent = tag.m_parentName;
        pa.m_child = tag.m_name;
        graph.push_back(pa);
    }
    m_notebook->updateTagGraph(tagGraphToString(graph));
}

bool NotebookTagMgr::removeTag(const QString &p_name)
{
    const auto nodePaths = findNodesOfTag(p_name);

    auto db = m_notebook->getDatabaseAccess();
    QStringList tagsAndChildren;
    if (!nodePaths.isEmpty()) {
        tagsAndChildren = db->queryTagAndChildren(p_name);
        if (tagsAndChildren.isEmpty()) {
            qWarning() << "failed to query tag and its children" << p_name;
            return false;
        }
    }

    if (!db->removeTag(p_name)) {
        return false;
    }

    const auto allTags = db->getAllTags();
    update(allTags);

    updateNotebookTagGraph(allTags);

    // Update node tag.
    for (const auto &pa : nodePaths) {
        auto node = m_notebook->loadNodeByPath(pa);
        if (!node) {
            qWarning() << "node belongs to tag in DB but not exists" << p_name << pa;
            continue;
        }

        const auto &tags = node->getTags();
        QStringList newTags;
        for (const auto &tag : tags) {
            if (tagsAndChildren.contains(tag)) {
                continue;
            }
            newTags.append(tag);
        }
        node->updateTags(newTags);
    }

    emit m_notebook->tagsUpdated();
    return true;
}

bool NotebookTagMgr::moveTag(const QString &p_name, const QString &p_newParentName)
{
    auto db = m_notebook->getDatabaseAccess();
    if (!db->addTag(p_name, p_newParentName)) {
        return false;
    }

    const auto allTags = db->getAllTags();
    update(allTags);

    updateNotebookTagGraph(allTags);

    emit m_notebook->tagsUpdated();
    return true;
}
