#ifndef TOOLBARHELPER_H
#define TOOLBARHELPER_H

#include <QHash>
#include <QIcon>

class QToolBar;

namespace vnotex
{
    class MainWindow;

    // Tool bar helper for MainWindow.
    class ToolBarHelper
    {
    public:
        explicit ToolBarHelper(MainWindow *p_mainWindow);

        // Setup all tool bars of main window.
        void setupToolBars();

        // Setup tool bars of main window into one unified tool bar.
        void setupToolBars(QToolBar *p_toolBar);

        static QIcon generateIcon(const QString &p_iconName);

        static QIcon generateDangerousIcon(const QString &p_iconName);

        static void addSpacer(QToolBar *p_toolBar);

    private:
        static QToolBar *setupFileToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static QToolBar *setupQuickAccessToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        static QToolBar *setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar);

        MainWindow *m_mainWindow = nullptr;

        QHash<QString, QToolBar *> m_toolBars;
    };
} // ns vnotex

#endif // TOOLBARHELPER_H
