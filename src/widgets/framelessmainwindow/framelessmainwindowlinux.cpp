#include "framelessmainwindowlinux.h"

#ifndef Q_OS_WIN

#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>

using namespace vnotex;

FramelessMainWindowLinux::FramelessMainWindowLinux(bool p_frameless, QWidget *p_parent)
    : FramelessMainWindow(p_frameless, p_parent) {
  if (m_frameless) {
    // installEventFilter(this);
  }
}

bool FramelessMainWindowLinux::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (!m_frameless) {
    return FramelessMainWindow::eventFilter(p_obj, p_event);
  }

  if (p_obj == m_titleBar) {
    const auto type = p_event->type();
    if (type == QEvent::MouseButtonDblClick || type == QEvent::NonClientAreaMouseButtonDblClick) {
      if (!(m_windowStates & Qt::WindowFullScreen)) {
        if (m_windowStates & Qt::WindowMaximized) {
          showNormal();
        } else {
          showMaximized();
        }
      }
    }
  } else if (p_obj == this) {
    doResize(p_event);
  }

  return FramelessMainWindow::eventFilter(p_obj, p_event);
}

void FramelessMainWindowLinux::setTitleBar(QWidget *p_titleBar) {
  FramelessMainWindow::setTitleBar(p_titleBar);

  m_titleBar->installEventFilter(this);
}

void FramelessMainWindowLinux::mousePressEvent(QMouseEvent *p_event) {
  FramelessMainWindow::mousePressEvent(p_event);

  if (m_frameless) {
    recordMousePress(p_event->pos());
  }
}

void FramelessMainWindowLinux::recordMousePress(const QPoint &p_pos) {
  m_mousePressed = true;
  m_mousePressPoint = p_pos;
  m_rectOnMousePress = geometry();
  m_mousePressArea = hitArea(m_mousePressPoint);
}

void FramelessMainWindowLinux::mouseReleaseEvent(QMouseEvent *p_event) {
  FramelessMainWindow::mouseReleaseEvent(p_event);

  if (m_frameless) {
    // setCursor(Qt::ArrowCursor);
    m_mousePressed = false;
  }
}

void FramelessMainWindowLinux::mouseMoveEvent(QMouseEvent *p_event) {
  FramelessMainWindow::mouseMoveEvent(p_event);

  if (m_frameless) {
    Q_ASSERT(m_mousePressed);
    if (m_movable && m_mousePressArea == Area::Caption) {
      const auto delta = p_event->pos() - m_mousePressPoint;
      move(pos() + delta);
    }
  }
}

void FramelessMainWindowLinux::doResize(QEvent *p_event) {
  Q_UNUSED(p_event);
#if 0
    static bool needResetCursor = false;

    switch (p_event->type()) {
    case QEvent::HoverMove:
    {
        if (!m_resizable) {
            return;
        }

        auto eve = static_cast<QHoverEvent *>(p_event);
        const auto evePos = eve->pos();
        const auto area = hitArea(evePos);
        if (!m_mousePressed && area != Area::None && area != Area::Caption) {
            // Mouse press event may be blocked by the children of QMainWindow.
            if (QGuiApplication::mouseButtons() != Qt::NoButton) {
                recordMousePress(evePos);
                return;
            }
        }

        if (m_mousePressed) {
            const auto delta = evePos - m_mousePressPoint;
            switch (m_mousePressArea) {
            case Area::Left:
            {
                int newWidth = m_rectOnMousePress.width() - delta.x();
                qDebug() << newWidth << minimumWidth();
                if (minimumWidth() <= newWidth) {
                    setGeometry(m_rectOnMousePress.x() + delta.x(),
                                m_rectOnMousePress.y(),
                                newWidth,
                                m_rectOnMousePress.height());
                }
                break;
            }

            default:
                break;
            }
        } else {
            switch (area) {
            case Area::Left:
                Q_FALLTHROUGH();
            case Area::Right:
                needResetCursor = true;
                setCursor(Qt::SizeHorCursor);
                break;

            case Area::Top:
                Q_FALLTHROUGH();
            case Area::Bottom:
                needResetCursor = true;
                setCursor(Qt::SizeVerCursor);
                break;

            case Area::TopLeft:
                Q_FALLTHROUGH();
            case Area::BottomRight:
                needResetCursor = true;
                setCursor(Qt::SizeFDiagCursor);
                break;

            case Area::TopRight:
                Q_FALLTHROUGH();
            case Area::BottomLeft:
                needResetCursor = true;
                setCursor(Qt::SizeBDiagCursor);
                break;

            default:
                if (needResetCursor) {
                    needResetCursor = false;
                    setCursor(Qt::ArrowCursor);
                }
                break;
            }
        }

        return;
    }

    default:
        break;
    }
#endif
}

FramelessMainWindowLinux::Area FramelessMainWindowLinux::hitArea(const QPoint &p_pos) const {
  const int x = p_pos.x();
  const int y = p_pos.y();
  Area area = Area::None;
  if (x < m_resizeAreaWidth) {
    // Left.
    if (y < m_resizeAreaWidth) {
      // Top.
      area = Area::TopLeft;
    } else if (y > height() - m_resizeAreaWidth) {
      // Bottom.
      area = Area::BottomLeft;
    } else {
      area = Area::Left;
    }
  } else if (x > width() - m_resizeAreaWidth) {
    // Right.
    if (y < m_resizeAreaWidth) {
      // Top.
      area = Area::TopRight;
    } else if (y > height() - m_resizeAreaWidth) {
      // Bottom.
      area = Area::BottomRight;
    } else {
      area = Area::Right;
    }
  } else if (y < m_resizeAreaWidth) {
    // Top.
    area = Area::Top;
  } else if (y > height() - m_resizeAreaWidth) {
    // Bottom.
    area = Area::Bottom;
  } else if (y < m_titleBarHeight) {
    area = Area::Caption;
  }

  return area;
}

void FramelessMainWindowLinux::showEvent(QShowEvent *p_event) {
  FramelessMainWindow::showEvent(p_event);

  if (m_frameless && m_titleBarHeight == 0 && m_titleBar) {
    m_titleBarHeight = m_titleBar->height();
  }
}

#endif
