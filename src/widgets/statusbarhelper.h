#ifndef STATUSBARHELPER_H
#define STATUSBARHELPER_H

namespace vnotex
{
    class MainWindow;

    class StatusBarHelper
    {
    public:
        StatusBarHelper() = delete;

        static void setupStatusBar(MainWindow *p_mainWindow);
    };
} // ns vnotex

#endif // STATUSBARHELPER_H
