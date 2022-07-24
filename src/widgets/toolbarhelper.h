#ifndef TOOLBARHELPER_H
#define TOOLBARHELPER_H

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
        ToolBarHelper() = delete;

        // Setup all tool bars of main window.
        static void setupToolBars(MainWindow *p_mainWindow);

        // Setup tool bars of main window into one unified tool bar.
        static void setupToolBars(MainWindow *p_mainWindow, QToolBar *p_toolBar);

        static QIcon generateIcon(const QString &p_iconName);

        static QIcon generateDangerousIcon(const QString &p_iconName);

        static void addSpacer(QToolBar *p_toolBar);

    private:
        static QToolBar *setupFileToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static QToolBar *setupQuickAccessToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static void setupTaskMenu(QMenu *p_menu);

        static void setupTaskActionMenu(QMenu *p_menu);

        static void addTaskMenu(QMenu *p_menu, Task *p_task);

        static QToolBar *setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static void updateQuickAccessMenu(QMenu *p_menu);

        static void setupExpandButton(MainWindow *p_win, QToolBar *p_toolBar);

        static void setupMenuButton(MainWindow *p_win, QToolBar *p_toolBar);

        static void activateQuickAccess(const QString &p_file);
    };
} // ns vnotex

#endif // TOOLBARHELPER_H
