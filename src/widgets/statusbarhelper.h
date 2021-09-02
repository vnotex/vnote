#ifndef STATUSBARHELPER_H
#define STATUSBARHELPER_H

class QStatusBar;
class QWidget;

namespace vnotex
{
    class MainWindow;

    class StatusBarHelper
    {
    public:
        explicit StatusBarHelper(MainWindow *p_mainWindow);

        void setupStatusBar();

    private:
        MainWindow *m_mainWindow = nullptr;

        QStatusBar *m_statusBar;
    };
} // ns vnotex

#endif // STATUSBARHELPER_H
