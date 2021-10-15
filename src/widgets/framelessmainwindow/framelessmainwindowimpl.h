#ifndef FRAMELESSMAINWINDOWIMPL_H
#define FRAMELESSMAINWINDOWIMPL_H

#include "framelessmainwindowwin.h"

namespace vnotex
{
#ifdef Q_OS_WIN
    typedef FramelessMainWindowWin FramelessMainWindowImpl;
#else
    typedef FramelessMainWindow FramelessMainWindowImpl;
#endif
}

#endif // FRAMELESSMAINWINDOWIMPL_H
