#ifndef SYSTEMTRAYHELPER_H
#define SYSTEMTRAYHELPER_H

class QSystemTrayIcon;

namespace vnotex
{
    class MainWindow;

    class SystemTrayHelper
    {
    public:
        SystemTrayHelper() = delete;

        static QSystemTrayIcon *setupSystemTray(MainWindow *p_win);
    };
}

#endif // SYSTEMTRAYHELPER_H
