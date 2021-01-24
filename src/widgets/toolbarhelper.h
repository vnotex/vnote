#ifndef TOOLBARHELPER_H
#define TOOLBARHELPER_H

#include <QHash>
#include <QIcon>

class QToolBar;
class QMenu;

namespace vnotex
{
    class MainWindow;
    class Task;

    // Tool bar helper for MainWindow.
    class ToolBarHelper
    {
    public:
        // Setup all tool bars of main window.
        void setupToolBars(MainWindow *p_win);

        // Setup tool bars of main window into one unified tool bar.
        void setupToolBars(MainWindow *p_win, QToolBar *p_toolBar);

        static QIcon generateIcon(const QString &p_iconName);

        static QIcon generateDangerousIcon(const QString &p_iconName);

        static void addSpacer(QToolBar *p_toolBar);

    private:
        static QToolBar *setupFileToolBar(MainWindow *p_win, QToolBar *p_toolBar);
        
        static QMenu *setupTaskMenu(QToolBar *p_tb);
        
        static void addTaskMenu(QMenu *p_menu, Task *p_task);

        static QToolBar *setupQuickAccessToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static QToolBar *setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        QHash<QString, QToolBar *> m_toolBars;
    };
} // ns vnotex

#endif // TOOLBARHELPER_H
