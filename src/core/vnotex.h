#ifndef VNOTEX_H
#define VNOTEX_H

#include <QObject>
#include <QScopedPointer>

#include "thememgr.h"
#include "global.h"

namespace vnotex
{
    class MainWindow;
    class NotebookMgr;
    class BufferMgr;
    class Node;
    struct FileOpenParameters;
    class Event;
    class Notebook;

    class VNoteX : public QObject
    {
        Q_OBJECT
    public:
        static VNoteX &getInst()
        {
            static VNoteX inst;
            return inst;
        }

        VNoteX(const VNoteX &) = delete;
        void operator=(const VNoteX &) = delete;

        // MUST be called to load some heavy data.
        // It is good to call it after MainWindow is shown.
        void initLoad();

        ThemeMgr &getThemeMgr() const;

        void setMainWindow(MainWindow *p_mainWindow);
        MainWindow *getMainWindow() const;

        NotebookMgr &getNotebookMgr() const;

        BufferMgr &getBufferMgr() const;

        ID getInstanceId() const;

    public slots:
        void showStatusMessage(const QString &p_message, int timeoutMilliseconds = 0);

        void showStatusMessageShort(const QString &p_message);

    signals:
        // Requested to new a notebook.
        void newNotebookRequested();

        // Requested to new a notebook from existing folder.
        void newNotebookFromFolderRequested();

        // Requested to import a notebook.
        void importNotebookRequested();

        // Requested to import a legacy notebook from VNote 2.0.
        void importLegacyNotebookRequested();

        // Requested to import files.
        void importFileRequested();

        // Requested to import folder.
        void importFolderRequested();

        // Requested to new a note in current notebook.
        // The handler should determine in which folder this note belongs to.
        void newNoteRequested();

        // Requested to new a folder in current notebook.
        void newFolderRequested();

        // Requested to show status message.
        void statusMessageRequested(const QString &p_message, int timeoutMilliseconds);

        // Requested to open @p_node.
        void openNodeRequested(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

        // @m_response of @p_event: true to continue the move, false to cancel the move.
        void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);

        // @m_response of @p_event: true to continue the removal, false to cancel the removal.
        void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);

        // @m_response of @p_event: true to continue the rename, false to cancel the rename.
        void nodeAboutToRename(Node *p_node, const QSharedPointer<Event> &p_event);

        // Requested to open @p_filePath.
        void openFileRequested(const QString &p_filePath, const QSharedPointer<FileOpenParameters> &p_paras);

        // Requested to locate node in explorer.
        void locateNodeRequested(Node *p_node);

        void exportRequested();

    private:
        explicit VNoteX(QObject *p_parent = nullptr);

        void initThemeMgr();

        void initNotebookMgr();

        void initBufferMgr();

        void initDocsUtils();

        MainWindow *m_mainWindow;

        // QObject managed.
        ThemeMgr *m_themeMgr;

        // QObject managed.
        NotebookMgr *m_notebookMgr;

        // QObject managed.
        BufferMgr *m_bufferMgr;

        // Used to identify app's instance.
        ID m_instanceId = 0;
    };
} // ns vnotex

#endif // VNOTEX_H
