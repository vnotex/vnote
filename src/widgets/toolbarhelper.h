#ifndef TOOLBARHELPER_H
#define TOOLBARHELPER_H

#include <QIcon>

class QToolBar;
class QMenu;

namespace vnotex
{
    class MainWindow;

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

        static QToolBar *setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static void updateQuickAccessMenu(QMenu *p_menu);
    };
} // ns vnotex

#endif // TOOLBARHELPER_H
