#ifndef FRAMELESSMAINWINDOWIMPL_H
#define FRAMELESSMAINWINDOWIMPL_H

#include "framelessmainwindowlinux.h"
#include "framelessmainwindowwin.h"

namespace vnotex {
#ifdef Q_OS_WIN
typedef FramelessMainWindowWin FramelessMainWindowImpl;
#else
typedef FramelessMainWindowLinux FramelessMainWindowImpl;
#endif
} // namespace vnotex

#endif // FRAMELESSMAINWINDOWIMPL_H
