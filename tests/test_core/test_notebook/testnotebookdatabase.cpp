#include "testnotebookdatabase.h"

#include <QtTest>

#include "dummynode.h"
#include "dummynotebook.h"

using namespace tests;

using namespace vnotex;

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
    QScopedPointer<DummyNode> node4(new DummyNode(Node::Flag::Content, 11, "ca", m_notebook.data(), node3.data()));
    addAndQueryNode(node4.data(), false);

    // Node 5, deep level.
    QScopedPointer<DummyNode> node5(new DummyNode(Node::Flag::Content, 60, "caa", m_notebook.data(), node4.data()));
    addAndQueryNode(node5.data(), false);

    // Node 6, deep level.
    QScopedPointer<DummyNode> node6(new DummyNode(Node::Flag::Content, 5, "cab", m_notebook.data(), node4.data()));
    addAndQueryNode(node6.data(), false);

    // queryNodePath().
    {
        testQueryNodePath(rootNode.data());
        testQueryNodePath(node1.data());
        testQueryNodePath(node2.data());
        testQueryNodePath(node3.data());
        testQueryNodePath(node4.data());
        testQueryNodePath(node5.data());
        testQueryNodePath(node6.data());
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

void TestNotebookDatabase::testQueryNodePath(const vnotex::Node *p_node)
{
    auto nodePath = m_dbAccess->queryNodePath(p_node->getId());
    auto node = p_node;
    for (int i = nodePath.size() - 1; i >= 0; --i) {
        QVERIFY(node);
        QCOMPARE(nodePath[i], node->getName());
        node = node->getParent();
    }
    QVERIFY(m_dbAccess->checkNodePath(p_node, nodePath));
}
