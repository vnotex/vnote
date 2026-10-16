#ifndef STATUSOVERLAY_H
#define STATUSOVERLAY_H

#include <QEvent>
#include <QLayout>
#include <QPointer>
#include <QScopedValueRollback>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <QBitmap>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <qt_windows.h>
#endif

namespace vnotex {
// Positions an existing status widget above the content, outside the window's layout.
// The attachment follows the status widget's lifetime, including shared ownership.
class StatusOverlay : public QObject {
public:
  StatusOverlay(QWidget *p_status, QLayout *p_contentLayout)
      : QObject(p_status), m_status(p_status), m_contentLayout(p_contentLayout),
        m_window(p_contentLayout->parentWidget()) {
    m_status->setParent(m_window);
    m_status->setAutoFillBackground(true);
    m_status->setAttribute(Qt::WA_StyledBackground);
    m_status->setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_WIN
    // Alien siblings cannot paint over WebEngine with native siblings disabled.
    m_status->setAttribute(Qt::WA_NativeWindow);
#endif
    m_window->installEventFilter(this);
    m_status->installEventFilter(this);
    watchContent();
    reposition();
  }

  static void setInputTransparent(QWidget *p_widget, bool p_transparent) {
    p_widget->setAttribute(Qt::WA_TransparentForMouseEvents, p_transparent);
#ifdef Q_OS_WIN
    if (p_widget->testAttribute(Qt::WA_NativeWindow) && !p_widget->isWindow()) {
      // Qt's WindowTransparentForInput handling is for top-level windows. Our
      // child HWND must also be skipped by native hit testing above WebEngine.
      const auto handle = reinterpret_cast<HWND>(p_widget->winId());
      const auto style = GetWindowLongPtr(handle, GWL_EXSTYLE);
      const auto newStyle = p_transparent ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
      if (style != newStyle) {
        SetWindowLongPtr(handle, GWL_EXSTYLE, newStyle);
      }
    }
#endif
  }

protected:
  bool eventFilter(QObject *p_watched, QEvent *p_event) override {
    if (!m_window || !m_contentLayout || m_status->parentWidget() != m_window) {
      return false;
    }
    const bool styleChanged = p_watched == m_status &&
                              (p_event->type() == QEvent::StyleChange ||
                               p_event->type() == QEvent::FontChange ||
                               p_event->type() == QEvent::PaletteChange);
#ifdef Q_OS_WIN
    if (styleChanged) {
      m_maskDirty = true;
    }
#endif
    if (styleChanged || (p_watched == m_window && p_event->type() == QEvent::LayoutRequest)) {
      // Event filters run before Qt polishes widgets and activates layouts. Use
      // their final geometry rather than caching a temporarily empty content area.
      if (p_watched == m_window) {
        watchContent();
      }
      if (!m_styleUpdatePending) {
        m_styleUpdatePending = true;
        QTimer::singleShot(0, this, [this] {
          m_styleUpdatePending = false;
          reposition();
        });
      }
      return false;
    }
    switch (p_event->type()) {
    case QEvent::WinIdChange:
      // A late WebEngine subtree can be stacked above an already-visible status.
      QTimer::singleShot(0, this, [this] { reposition(); });
      break;
    case QEvent::Show:
    case QEvent::ShowToParent:
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::StyleChange:
    case QEvent::FontChange:
    case QEvent::PaletteChange:
      if (p_watched == m_window) {
        watchContent();
      }
      reposition();
      break;
    default:
      break;
    }
    return false;
  }

private:
  void watchContent() {
    for (int idx = 0; idx < m_contentLayout->count(); ++idx) {
      if (auto *widget = m_contentLayout->itemAt(idx)->widget()) {
        widget->installEventFilter(this);
      }
    }
  }

#ifdef Q_OS_WIN
  void updateMask() {
    const QSize size = m_status->size();
    if (m_styleUpdatePending || size.isEmpty() || (!m_maskDirty && m_maskSize == size)) {
      return;
    }
    // Sample only the QSS surface, not the children. QSS owns the radius; the
    // native child HWND must follow that shape to reveal WebEngine at its corners.
    QImage background(size, QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::transparent);
    {
      QStyleOption option;
      option.initFrom(m_status);
      QPainter painter(&background);
      m_status->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, m_status);
    }
    const QRegion mask(QBitmap::fromImage(background.createAlphaMask(Qt::ThresholdAlphaDither)));
    if (mask.isEmpty()) {
      m_status->clearMask();
    } else {
      m_status->setMask(mask);
    }
    m_maskSize = size;
    m_maskDirty = false;
  }
#endif

  void reposition() {
    if (m_positioning || m_styleUpdatePending || !m_window || !m_contentLayout ||
        m_status->parentWidget() != m_window) {
      return;
    }
    QScopedValueRollback<bool> guard(m_positioning, true);
    m_status->ensurePolished();
    // A child size-hint change invalidates the content layout before LayoutRequest.
    // Activate the owning layout before reading its geometry, not an empty cache.
    if (auto *layout = m_window->layout()) {
      layout->activate();
    }
    const QRect area = m_contentLayout->geometry().intersected(m_window->contentsRect());
    const int height = qBound(0, m_status->sizeHint().height(), area.height());
    const QRect geometry(area.left(), area.bottom() + 1 - height, area.width(), height);
    if (m_status->geometry() != geometry) {
      m_status->setGeometry(geometry);
    }
#ifdef Q_OS_WIN
    if (!m_status->isHidden()) {
      updateMask();
    }
#endif
    setInputTransparent(m_status, m_status->testAttribute(Qt::WA_TransparentForMouseEvents));
    m_status->raise();
#ifdef Q_OS_WIN
    // QWidget's sibling bookkeeping can disagree with newly created HWNDs.
    if (auto *handle = m_status->windowHandle()) {
      handle->raise();
    }
#endif
  }

  QWidget *m_status;
  QPointer<QLayout> m_contentLayout;
  QPointer<QWidget> m_window;
  bool m_positioning = false;
  bool m_styleUpdatePending = false;
#ifdef Q_OS_WIN
  QSize m_maskSize;
  bool m_maskDirty = true;
#endif
};
} // namespace vnotex

#endif // STATUSOVERLAY_H
