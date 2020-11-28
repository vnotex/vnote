#ifndef NOTEBOOKEXPLORER_H
#define NOTEBOOKEXPLORER_H

#include <QFrame>
#include <QSharedPointer>

#include "global.h"

namespace vnotex
{
    class Notebook;
    class TitleBar;
    class NotebookSelector;
    class NotebookNodeExplorer;
    class Node;

    class NotebookExplorer : public QFrame
    {
        Q_OBJECT
    public:
        explicit NotebookExplorer(QWidget *p_parent = nullptr);

    public slots:
        void newNotebook();

        void newNotebookFromFolder();

        void importNotebook();

        void loadNotebooks();

        void reloadNotebook(const Notebook *p_notebook);

        void setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook);

        void newFolder();

        void newNote();

        void importFile();

        void importFolder();

        void importLegacyNotebook();

        void locateNode(Node *p_node);

    signals:
        void notebookActivated(ID p_notebookId);

        // Internal use only.
        void updateTitleBarMenuActions();

    private:
        void setupUI();

        TitleBar *setupTitleBar(QWidget *p_parent = nullptr);

        Node *currentExploredFolderNode() const;

        Node *checkNotebookAndGetCurrentExploredFolderNode() const;

        NotebookSelector *m_selector = nullptr;

        NotebookNodeExplorer *m_nodeExplorer = nullptr;

        QSharedPointer<Notebook> m_currentNotebook;
    };
} // ns vnotex

#endif // NOTEBOOKEXPLORER_H
