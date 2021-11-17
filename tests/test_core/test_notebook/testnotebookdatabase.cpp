#include "testnotebookdatabase.h"

#include <QtTest>

#include "dummynode.h"
#include "dummynotebook.h"

using namespace tests;

using namespace vnotex;

template <typename T>
static void checkStringListEqual(T p_actual, T p_expected)
{
    std::sort(p_actual.begin(), p_actual.end());
    std::sort(p_expected.begin(), p_expected.end());
    QCOMPARE(p_actual, p_expected);
}

TestNotebookDatabase::TestNotebookDatabase()
{
    QVERIFY(m_testDir.isValid());

    m_notebook.reset(new DummyNotebook("test_notebook"));

    m_dbAccess.reset(new NotebookDatabaseAccess(m_notebook.data(), m_testDir.filePath("test.db")));

    m_dbAccess->initialize(0);
    QVERIFY(m_dbAccess->isFresh());
    QVERIFY(m_dbAccess->isValid());
}

TestNotebookDatabase::~TestNotebookDatabase()
{
    m_dbAccess->close();
    m_dbAccess.reset();
}

void TestNotebookDatabase::test()
{
    testNode();

    testTag();

    testNodeTag();
}

void TestNotebookDatabase::testNode()
{
    // Invlaid node.
    {
        auto nodeRec = m_dbAccess->queryNode(1);
        QVERIFY(nodeRec == nullptr);
    }

    // Root node.
    QScopedPointer<DummyNode> rootNode(new DummyNode(Node::Flag::Container, 0, "", m_notebook.data(), nullptr));
    addAndQueryNode(rootNode.data(), true);

    // Node 1.
    QScopedPointer<DummyNode> node1(new DummyNode(Node::Flag::Content, 10, "a", m_notebook.data(), rootNode.data()));
    addAndQueryNode(node1.data(), true);

    // Node 2, respect id.
    QScopedPointer<DummyNode> node2(new DummyNode(Node::Flag::Content, 50, "b", m_notebook.data(), rootNode.data()));
    addAndQueryNode(node2.data(), false);
    QCOMPARE(node2->getId(), 50);

    // Node 3, respect id with invalid id.
    QScopedPointer<DummyNode> node3(new DummyNode(Node::Flag::Container, 0, "c", m_notebook.data(), rootNode.data()));
    addAndQueryNode(node3.data(), false);
    QVERIFY(node3->getId() != 0);

    // Node 4, deep level.
    QScopedPointer<DummyNode> node4(new DummyNode(Node::Flag::Container, 11, "ca", m_notebook.data(), node3.data()));
    addAndQueryNode(node4.data(), false);

    // Node 5, deep level.
    QScopedPointer<DummyNode> node5(new DummyNode(Node::Flag::Content, 60, "caa", m_notebook.data(), node4.data()));
    addAndQueryNode(node5.data(), false);

    // Node 6, deep level.
    QScopedPointer<DummyNode> node6(new DummyNode(Node::Flag::Content, 5, "cab", m_notebook.data(), node4.data()));
    addAndQueryNode(node6.data(), false);

    // Node 7/8, with non-exist parent.
    QScopedPointer<DummyNode> node7(new DummyNode(Node::Flag::Content, 55, "caba", m_notebook.data(), node6.data()));
    QScopedPointer<DummyNode> node8(new DummyNode(Node::Flag::Content, 555, "cabaa", m_notebook.data(), node7.data()));
    {
        bool ret = m_dbAccess->addNode(node8.data(), false);
        QVERIFY(!ret);

        ret = m_dbAccess->addNodeRecursively(node8.data(), false);
        queryAndVerifyNode(node7.data());
        QVERIFY(m_dbAccess->existsNode(node7.data()));
        queryAndVerifyNode(node8.data());
        QVERIFY(m_dbAccess->existsNode(node8.data()));
    }

    // queryNodeParentPath().
    {
        testQueryNodeParentPath(rootNode.data());
        testQueryNodeParentPath(node1.data());
        testQueryNodeParentPath(node2.data());
        testQueryNodeParentPath(node3.data());
        testQueryNodeParentPath(node4.data());
        testQueryNodeParentPath(node5.data());
        testQueryNodeParentPath(node6.data());
    }

    // updateNode().
    {
        node6->setParent(node5.data());
        node6->setName("caaa");
        bool ret = m_dbAccess->updateNode(node6.data());
        QVERIFY(ret);
        queryAndVerifyNode(node6.data());
    }

    // removeNode().
    {
        QVERIFY(m_dbAccess->existsNode(node6.data()));
        bool ret = m_dbAccess->removeNode(node6->getId());
        QVERIFY(ret);
        QVERIFY(!m_dbAccess->existsNode(node6.data()));

        // DELETE CASCADE.
        QVERIFY(m_dbAccess->existsNode(node3.data()));
        QVERIFY(m_dbAccess->existsNode(node4.data()));
        QVERIFY(m_dbAccess->existsNode(node5.data()));
        ret = m_dbAccess->removeNode(node3->getId());
        QVERIFY(ret);
        QVERIFY(!m_dbAccess->existsNode(node3.data()));
        QVERIFY(!m_dbAccess->existsNode(node4.data()));
        QVERIFY(!m_dbAccess->existsNode(node5.data()));

        // Add back nodes.
        addAndQueryNode(node3.data(), false);
        addAndQueryNode(node4.data(), false);
        addAndQueryNode(node5.data(), false);
        addAndQueryNode(node6.data(), false);
    }
}

void TestNotebookDatabase::addAndQueryNode(Node *p_node, bool p_ignoreId)
{
    bool ret = m_dbAccess->addNode(p_node, p_ignoreId);
    QVERIFY(ret);
    QVERIFY(p_node->getId() != NotebookDatabaseAccess::InvalidId);
    queryAndVerifyNode(p_node);
    QVERIFY(m_dbAccess->existsNode(p_node));
}

void TestNotebookDatabase::queryAndVerifyNode(const vnotex::Node *p_node)
{
    auto nodeRec = m_dbAccess->queryNode(p_node->getId());
    QVERIFY(nodeRec);
    QCOMPARE(nodeRec->m_id, p_node->getId());
    QCOMPARE(nodeRec->m_name, p_node->getName());
    QCOMPARE(nodeRec->m_signature, p_node->getSignature());
    QCOMPARE(nodeRec->m_parentId, p_node->getParent() ? p_node->getParent()->getId() : NotebookDatabaseAccess::InvalidId);
}

void TestNotebookDatabase::testQueryNodeParentPath(const vnotex::Node *p_node)
{
    auto nodePath = m_dbAccess->queryNodeParentPath(p_node->getId());
    auto node = p_node;
    for (int i = nodePath.size() - 1; i >= 0; --i) {
        QVERIFY(node);
        QCOMPARE(nodePath[i], node->getName());
        node = node->getParent();
    }
    QVERIFY(m_dbAccess->checkNodePath(p_node, nodePath));

    QCOMPARE(m_dbAccess->queryNodePath(p_node->getId()), p_node->fetchPath());
}

void TestNotebookDatabase::testTag()
{
    // Invalid tag.
    {
        auto nodeRec = m_dbAccess->queryTag("1");
        QVERIFY(nodeRec == nullptr);
    }

    // Tag 1.
    const QString tag1("1");
    addAndQueryTag(tag1, "");

    // Tag 2.
    QString tag2("2");
    addAndQueryTag(tag2, "");

    // Tag 3 as child of tag 2.
    QString tag3("21");
    addAndQueryTag(tag3, tag2);
    checkStringListEqual({tag2, tag3}, m_dbAccess->queryTagAndChildren(tag2));

    // Tag 4 as child of tag 2.
    const QString tag4("22");
    addAndQueryTag(tag4, tag2);
    checkStringListEqual({tag2, tag3, tag4}, m_dbAccess->queryTagAndChildren(tag2));

    // Tag 5 as child of tag 4.
    const QString tag5("221");
    addAndQueryTag(tag5, tag4);
    checkStringListEqual({tag4, tag5}, m_dbAccess->queryTagAndChildren(tag4));
    checkStringListEqual({tag2, tag3, tag4, tag5}, m_dbAccess->queryTagAndChildren(tag2));

    // Add with update.
    addAndQueryTag(tag3, tag1);

    // Add without update.
    {
        bool ret = m_dbAccess->addTag(tag3);
        QVERIFY(ret);
        queryAndVerifyTag(tag3, tag1);

        ret = m_dbAccess->addTag("3");
        QVERIFY(ret);
        queryAndVerifyTag("3", "");
    }

    // Rename.
    {
        bool ret = m_dbAccess->renameTag(tag3, "11");
        QVERIFY(ret);
        queryAndVerifyTag("11", tag1);

        // Tag should be gone.
        QVERIFY(!m_dbAccess->queryTag(tag3));
        tag3 = "11";

        ret = m_dbAccess->renameTag(tag2, "new2");
        QVERIFY(ret);
        queryAndVerifyTag("new2", "");

        QVERIFY(!m_dbAccess->queryTag(tag2));
        tag2 = "new2";

        queryAndVerifyTag(tag4, tag2);
        queryAndVerifyTag(tag5, tag4);
    }

    // removeTag().
    {
        bool ret = m_dbAccess->removeTag(tag3);
        QVERIFY(ret);
        QVERIFY(!m_dbAccess->queryTag(tag3));

        ret = m_dbAccess->removeTag(tag2);
        QVERIFY(ret);
        QVERIFY(!m_dbAccess->queryTag(tag2));
        QVERIFY(!m_dbAccess->queryTag(tag4));
        QVERIFY(!m_dbAccess->queryTag(tag5));

        // Add back tags.
        addAndQueryTag(tag3, tag1);
        addAndQueryTag(tag2, "");
        addAndQueryTag(tag4, tag2);
        addAndQueryTag(tag5, tag4);
    }
}

void TestNotebookDatabase::addAndQueryTag(const QString &p_name, const QString &p_parentName)
{
    bool ret = m_dbAccess->addTag(p_name, p_parentName);
    QVERIFY(ret);
    queryAndVerifyTag(p_name, p_parentName);
}

void TestNotebookDatabase::queryAndVerifyTag(const QString &p_name, const QString &p_parentName)
{
    auto tagRec = m_dbAccess->queryTag(p_name);
    QVERIFY(tagRec);
    QCOMPARE(tagRec->m_name, p_name);
    QCOMPARE(tagRec->m_parentName, p_parentName);
}

void TestNotebookDatabase::testNodeTag()
{
    // Dummy root.
    QScopedPointer<DummyNode> rootNode(new DummyNode(Node::Flag::Container, 1, "", m_notebook.data(), nullptr));

    // Node 10 -> tag1.
    QScopedPointer<DummyNode> node10(new DummyNode(Node::Flag::Content, 0, "o", m_notebook.data(), rootNode.data()));
    addAndQueryNode(node10.data(), true);
    node10->updateTags({"1"});
    updateNodeTagsAndCheck(node10.data());

    // Node 11 -> tag2, tag3, tag1.
    QScopedPointer<DummyNode> node11(new DummyNode(Node::Flag::Container, 0, "p", m_notebook.data(), rootNode.data()));
    addAndQueryNode(node11.data(), true);
    node11->updateTags({"new2", "11", "1"});
    updateNodeTagsAndCheck(node11.data());

    // Node 12 -> tag4, tag100.
    QScopedPointer<DummyNode> node12(new DummyNode(Node::Flag::Content, 0, "pa", m_notebook.data(), node11.data()));
    addAndQueryNode(node12.data(), true);
    node12->updateTags({"22", "100"});
    updateNodeTagsAndCheck(node12.data());

    // Node 13 -> tag5.
    QScopedPointer<DummyNode> node13(new DummyNode(Node::Flag::Content, 0, "pb", m_notebook.data(), node11.data()));
    addAndQueryNode(node13.data(), true);
    node13->updateTags({"221"});
    updateNodeTagsAndCheck(node13.data());

    checkStringListEqual(m_dbAccess->queryTagNodes("1"), {node10->getId(), node11->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodes("11"), {node11->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodes("new2"), {node11->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodes("22"), {node12->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodes("100"), {node12->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodes("221"), {node13->getId()});

    checkStringListEqual(m_dbAccess->queryTagNodesRecursive("1"), {node10->getId(), node11->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodesRecursive("new2"), {node11->getId(), node12->getId(), node13->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodesRecursive("22"), {node12->getId(), node13->getId()});
    checkStringListEqual(m_dbAccess->queryTagNodesRecursive("221"), {node13->getId()});
}

void TestNotebookDatabase::updateNodeTagsAndCheck(vnotex::Node *p_node)
{
    m_dbAccess->updateNodeTags(p_node);
    checkStringListEqual(m_dbAccess->queryNodeTags(p_node->getId()), p_node->getTags());
}
