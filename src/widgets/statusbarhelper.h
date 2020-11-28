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
        StatusBarHelper()
        {
        }

        void setupStatusBar(MainWindow *p_win);

    private:
        QStatusBar *m_statusBar;
    };
} // ns vnotex

#endif // STATUSBARHELPER_H
