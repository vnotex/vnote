#ifndef FRAMELESSMAINWINDOWIMPL_H
#define FRAMELESSMAINWINDOWIMPL_H

#include "framelessmainwindowwin.h"
#include "framelessmainwindowlinux.h"

namespace vnotex
{
#ifdef Q_OS_WIN
    typedef FramelessMainWindowWin FramelessMainWindowImpl;
#else
    typedef FramelessMainWindowLinux FramelessMainWindowImpl;
#endif
}

#endif // FRAMELESSMAINWINDOWIMPL_H
