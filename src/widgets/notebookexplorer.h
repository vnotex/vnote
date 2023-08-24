#ifndef NOTEBOOKEXPLORER_H
#define NOTEBOOKEXPLORER_H

#include <QFrame>
#include <QSharedPointer>

#include <core/global.h>

#include "notebookexplorersession.h"

class QMenu;

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

        const QSharedPointer<Notebook> &currentNotebook() const;

        Node *currentExploredFolderNode() const;

        Node *currentExploredNode() const;

        QByteArray saveState() const;

        void restoreState(const QByteArray &p_data);

    public slots:
        void newNotebook();

        void newNotebookFromFolder();

        void importNotebook();

        void loadNotebooks();

        void reloadNotebook(const Notebook *p_notebook);

        void setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook);

        void newFolder();

        void newNote();

        void newQuickNote();

        void importFile();

        void importFolder();

        void importLegacyNotebook();

        void locateNode(Node *p_node);

        void manageNotebooks();

    signals:
        void notebookActivated(ID p_notebookId);

    private:
        void setupUI();

        TitleBar *setupTitleBar(QWidget *p_parent = nullptr);

        Node *checkNotebookAndGetCurrentExploredFolderNode() const;

        void setupViewMenu(QMenu *p_menu, bool p_isNotebookView);

        void setupRecycleBinMenu(QMenu *p_menu);

        void setupExploreModeMenu(TitleBar *p_titleBar);

        void saveSession();

        void loadSession();

        void updateSession();

        void recoverSession();

        void rebuildDatabase();

        NotebookSelector *m_selector = nullptr;

        NotebookNodeExplorer *m_nodeExplorer = nullptr;

        QSharedPointer<Notebook> m_currentNotebook;

        NotebookExplorerSession m_session;

        bool m_sessionLoaded = false;
    };
} // ns vnotex

#endif // NOTEBOOKEXPLORER_H
