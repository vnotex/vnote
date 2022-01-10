#include "framelessmainwindowwin.h"

#ifdef Q_OS_WIN

#include <QTimer>
#include <QEvent>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment (lib,"dwmapi.lib")
#pragma comment (lib, "user32.lib")

using namespace vnotex;

FramelessMainWindowWin::FramelessMainWindowWin(bool p_frameless, QWidget *p_parent)
    : FramelessMainWindow(p_frameless, p_parent)
{
    if (m_frameless) {
        m_resizeAreaWidth *= devicePixelRatio();

        m_redrawTimer = new QTimer(this);
        m_redrawTimer->setSingleShot(true);
        m_redrawTimer->setInterval(500);
        connect(m_redrawTimer, &QTimer::timeout,
                this, &FramelessMainWindowWin::forceRedraw);

        connect(this, &FramelessMainWindow::windowStateChanged,
                this, &FramelessMainWindowWin::updateMargins);

        // Enable some window effects on Win, such as snap and maximizing.
        // It will activate the title bar again. Need to remove it in WM_NCCALCSIZE msg.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        DWORD style = ::GetWindowLong(hwnd, GWL_STYLE);
        ::SetWindowLong(hwnd, GWL_STYLE, style | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CAPTION);

        // Leave 1 pixel width of border so OS will draw a window shadow.
        const MARGINS shadow = {1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(hwnd, &shadow);
    }
}

#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
bool FramelessMainWindowWin::nativeEvent(const QByteArray &p_eventType, void *p_message, qintptr *p_result)
#else
bool FramelessMainWindowWin::nativeEvent(const QByteArray &p_eventType, void *p_message, long *p_result)
#endif
{
    if (!m_frameless) {
        return FramelessMainWindow::nativeEvent(p_eventType, p_message, p_result);
    }

    if (p_eventType == QStringLiteral("windows_generic_MSG")) {
        MSG *msg = static_cast<MSG *>(p_message);

        switch (msg->message) {
        case WM_NCCALCSIZE:
            *p_result = 0;
            return true;

        case WM_NCHITTEST:
        {
            if (m_windowStates & Qt::WindowFullScreen) {
                *p_result = HTCLIENT;
                return true;
            }

            RECT windowRect;
            ::GetWindowRect(msg->hwnd, &windowRect);

            // x and y could not be compared with width() and height() in hidpi case.
            const int x = static_cast<int>(GET_X_LPARAM(msg->lParam) - windowRect.left);
            const int y = static_cast<int>(GET_Y_LPARAM(msg->lParam) - windowRect.top);

            *p_result = 0;
            if (m_resizable) {
                if (x < m_resizeAreaWidth) {
                    // Left.
                    if (y < m_resizeAreaWidth) {
                        // Top.
                        *p_result = HTTOPLEFT;
                    } else if (y > windowRect.bottom - windowRect.top - m_resizeAreaWidth) {
                        // Bottom.
                        *p_result = HTBOTTOMLEFT;
                    } else {
                        *p_result = HTLEFT;
                    }
                } else if (x > windowRect.right - windowRect.left - m_resizeAreaWidth) {
                    // Right.
                    if (y < m_resizeAreaWidth) {
                        // Top.
                        *p_result = HTTOPRIGHT;
                    } else if (y > windowRect.bottom - windowRect.top - m_resizeAreaWidth) {
                        // Bottom.
                        *p_result = HTBOTTOMRIGHT;
                    } else {
                        *p_result = HTRIGHT;
                    }
                } else if (y < m_resizeAreaWidth) {
                    *p_result = HTTOP;
                } else if (y > windowRect.bottom - windowRect.top - m_resizeAreaWidth) {
                    *p_result = HTBOTTOM;
                }
            }

            if (0 != *p_result) {
                return true;
            }

            if (m_titleBar) {
                if (m_titleBarHeight == 0) {
                    m_titleBarHeight = m_titleBar->height() * devicePixelRatio();
                }

                if (y < m_titleBarHeight) {
                    QWidget *child = m_titleBar->childAt(m_titleBar->mapFromGlobal(QCursor::pos()));
                    if (!child) {
                        *p_result = HTCAPTION;
                        if (::GetAsyncKeyState(VK_LBUTTON) < 0 || ::GetAsyncKeyState(VK_RBUTTON) < 0) {
                            m_sizeBeforeMove = size();
                        }
                        return true;
                    }
                }
            }

            break;
        }

        case WM_POWERBROADCAST:
        {
            if (msg->wParam == PBT_APMSUSPEND) {
                // Minimize when system is going to sleep to avoid bugs.
                showMinimized();
            }
            break;
        }

        case WM_GETMINMAXINFO:
        {
            // When maximized, OS will expand the content area. To avoid missing the real contents, set extra margins.
            if (::IsZoomed(msg->hwnd)) {
                RECT frame = {0, 0, 0, 0};
                ::AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, false, 0);
                const int dpiScale = devicePixelRatio();
                // Use bottom as top.
                QMargins newMargins(qAbs(frame.left) / dpiScale,
                                    qAbs(frame.bottom) / dpiScale,
                                    frame.right / dpiScale,
                                    frame.bottom / dpiScale);
                if (newMargins != m_maximizedMargins) {
                    m_maximizedMargins = newMargins;
                    updateMargins();
                }
            }
            break;
        }

        default:
            break;
        }
    }

    return FramelessMainWindow::nativeEvent(p_eventType, p_message, p_result);
}

void FramelessMainWindowWin::moveEvent(QMoveEvent *p_event)
{
    FramelessMainWindow::moveEvent(p_event);

    if (m_frameless) {
        if (m_windowStates & Qt::WindowMaximized) {
            m_redrawTimer->stop();
        } else {
            m_redrawTimer->start();
        }
    }
}

void FramelessMainWindowWin::updateMargins()
{
    if (!m_frameless) {
        return;
    }

    int topMargin = 0;
    if (isMaximized()) {
        setContentsMargins(m_maximizedMargins);
        topMargin = m_maximizedMargins.top();
    } else {
        setContentsMargins(0, 0, 0, 0);
    }

    if (m_titleBar) {
        m_titleBarHeight = (m_titleBar->height() + topMargin) * devicePixelRatio();
    }
}

void FramelessMainWindowWin::forceRedraw()
{
    Q_ASSERT(m_frameless);
    if (m_windowStates & Qt::WindowMaximized) {
        return;
    }

    const QSize sz = size();
    RECT frame;
    ::GetWindowRect((HWND)winId(), &frame);
    const int clientWidth = (frame.right - frame.left) / devicePixelRatio();
    const int clientHeight = (frame.bottom - frame.top) / devicePixelRatio();
    if (clientWidth != sz.width() || clientHeight != sz.height()) {
        // resize() may result to "unable to set geometry" warning.
        // adjustsize() or resize() to another size before could solve this.
        resize(sz.width() + 1, sz.height() + 1);
        if (m_sizeBeforeMove.isEmpty()) {
            resize(clientWidth, clientHeight);
        } else {
            resize(m_sizeBeforeMove);
        }
    }
}

void FramelessMainWindowWin::setWindowFlagsOnUpdate()
{
    if (m_frameless) {
        // We need to re-set the window flags again in some cases, such as after StayOnTop.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        DWORD style = ::GetWindowLong(hwnd, GWL_STYLE);
        ::SetWindowLong(hwnd, GWL_STYLE, style | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CAPTION);
    }
}

#endif
