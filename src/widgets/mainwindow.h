#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSharedPointer>

#include "toolbarhelper.h"
#include "statusbarhelper.h"

class QDockWidget;
class QSystemTrayIcon;

namespace vnotex
{
    class ToolBox;
    class NotebookExplorer;
    class ViewArea;
    class Event;
    class OutlineViewer;

    enum { RESTART_EXIT_CODE = 1000 };

    class MainWindow : public QMainWindow
    {
        Q_OBJECT
    public:
        explicit MainWindow(QWidget *p_parent = nullptr);

        ~MainWindow();

        MainWindow(const MainWindow &) = delete;
        void operator=(const MainWindow &) = delete;

        void kickOffOnStart();

        void resetStateAndGeometry();

        const QVector<QDockWidget *> &getDocks() const;

        void setContentAreaExpanded(bool p_expanded);
        // Should be called after MainWindow is shown.
        bool isContentAreaExpanded() const;

        void focusViewArea();

        void setStayOnTop(bool p_enabled);

        void restart();

        void showMainWindow();

        void quitApp();

    signals:
        void mainWindowStarted();

        // @m_response of @p_event: true to continue the close, false to stop the close.
        void mainWindowClosed(const QSharedPointer<Event> &p_event);

        // No user interaction is available.
        void mainWindowClosedOnQuit();

        void layoutChanged();

    protected:
        void closeEvent(QCloseEvent *p_event) Q_DECL_OVERRIDE;

        void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void closeOnQuit();

        void updateTabBarStyle();

        void exportNotes();

    private:
        // Index in m_docks.
        enum DockIndex
        {
            NavigationDock = 0,
            OutlineDock
        };

        void setupUI();

        void setupCentralWidget();

        void setupNavigationToolBox();

        void setupOutlineViewer();

        void setupNavigationDock();

        void setupOutlineDock();

        void setupNotebookExplorer(QWidget *p_parent = nullptr);

        void setupDocks();

        void setupStatusBar();

        void saveStateAndGeometry();

        void loadStateAndGeometry();

        // Used to test widget in development.
        void demoWidget();

        QString getViewAreaTitle() const;

        void activateDock(QDockWidget *p_dock);

        void setupToolBar();

        void setupShortcuts();

        void setupSystemTray();

        ToolBarHelper m_toolBarHelper;

        StatusBarHelper m_statusBarHelper;

        ToolBox *m_navigationToolBox = nullptr;

        NotebookExplorer *m_notebookExplorer = nullptr;

        ViewArea *m_viewArea = nullptr;

        QWidget *m_viewAreaStatusWidget = nullptr;

        OutlineViewer *m_outlineViewer = nullptr;

        QVector<QDockWidget *> m_docks;

        bool m_layoutReset = false;

        // -1: do not request to quit;
        // 0 and above: exit code.
        int m_requestQuit = -1;

        Qt::WindowStates m_windowOldState = Qt::WindowMinimized;

        QSystemTrayIcon *m_trayIcon = nullptr;
    };
} // ns vnotex

#endif // MAINWINDOW_H
