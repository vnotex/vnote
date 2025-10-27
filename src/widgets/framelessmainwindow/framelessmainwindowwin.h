#ifndef FRAMELESSMAINWINDOWWIN_H
#define FRAMELESSMAINWINDOWWIN_H

#include "framelessmainwindow.h"

namespace vnotex {
#ifdef Q_OS_WIN
class FramelessMainWindowWin : public FramelessMainWindow {
  Q_OBJECT
public:
  FramelessMainWindowWin(bool p_frameless, QWidget *p_parent = nullptr);

protected:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  bool nativeEvent(const QByteArray &p_eventType, void *p_message,
                   qintptr *p_result) Q_DECL_OVERRIDE;
#else
  bool nativeEvent(const QByteArray &p_eventType, void *p_message, long *p_result) Q_DECL_OVERRIDE;
#endif

  void moveEvent(QMoveEvent *p_event) Q_DECL_OVERRIDE;

  void setWindowFlagsOnUpdate() Q_DECL_OVERRIDE;

private:
  // To fix some unkonwn bugs of the interface.
  void forceRedraw();

  void updateMargins();

  int m_resizeAreaWidth = 6;

  QTimer *m_redrawTimer = nullptr;

  QSize m_sizeBeforeMove;

  QMargins m_maximizedMargins;

  int m_titleBarHeight = 0;
};
#else
class FramelessMainWindowWinDummy {
public:
  FramelessMainWindowWinDummy() = default;
};
#endif
} // namespace vnotex

#endif // FRAMELESSMAINWINDOWWIN_H
