#ifndef TESTNOTEBOOKDATABASE_H
#define TESTNOTEBOOKDATABASE_H

#include <QScopedPointer>
#include <QTemporaryDir>

#include <notebook/notebookdatabaseaccess.h>

namespace tests
{
    class TestNotebookDatabase
    {
    public:
        TestNotebookDatabase();

        ~TestNotebookDatabase();

        void test();

    private:
        void testNode();

        void testTag();

        void testNodeTag();

    private:
        void addAndQueryNode(vnotex::Node *p_node, bool p_ignoreId);

        void testQueryNodeParentPath(const vnotex::Node *p_node);

        void queryAndVerifyNode(const vnotex::Node *p_node);

        void addAndQueryTag(const QString &p_name, const QString &p_parentName);

        void queryAndVerifyTag(const QString &p_name, const QString &p_parentName);

        void updateNodeTagsAndCheck(vnotex::Node *p_node);

        QTemporaryDir m_testDir;

        QScopedPointer<vnotex::Notebook> m_notebook;

        QScopedPointer<vnotex::NotebookDatabaseAccess> m_dbAccess;
    };
}

#endif // TESTNOTEBOOKDATABASE_H
