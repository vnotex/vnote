#include "imageviewer.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <array>

#include <utility>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>

#include <gui/utils/iconutils.h>

using namespace vnotex;

namespace {
class ImageGraphicsView : public QGraphicsView {
public:
  explicit ImageGraphicsView(QWidget *p_parent) : QGraphicsView(p_parent) {}

  void setOverlay(QWidget *p_overlay) { m_overlay = p_overlay; }

protected:
  void scrollContentsBy(int p_dx, int p_dy) override {
    // QGraphicsView's optimized QWidget::scroll also moves viewport children.
    const auto position = m_overlay ? m_overlay->pos() : QPoint();
    QGraphicsView::scrollContentsBy(p_dx, p_dy);
    if (m_overlay) {
      m_overlay->move(position);
    }
  }

private:
  QWidget *m_overlay = nullptr;
};
} // namespace

ImageViewer::ImageViewer(ServiceLocator &p_services, QWidget *p_parent, Mode p_mode)
    : QWidget(p_parent), m_services(p_services) {
  auto *view = new ImageGraphicsView(this);
  m_view = view;
  auto *scene = new QGraphicsScene(m_view);
  m_view->setScene(scene);
  m_image = scene->addPixmap(QPixmap());
  m_image->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
  m_image->setAcceptedMouseButtons(Qt::NoButton);
  m_view->setInteractive(false);
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
  m_view->setRenderHint(QPainter::SmoothPixmapTransform);
  m_view->setAlignment(Qt::AlignCenter);
  m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setTransformationAnchor(QGraphicsView::NoAnchor);
  m_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_view);
  setFocusProxy(m_view);

  m_error =
      new QLabel(QCoreApplication::translate("vnotex::ImageViewWindow", "Unable to display image"),
                 m_view->viewport());
  m_error->setTextFormat(Qt::PlainText);
  m_error->setAlignment(Qt::AlignCenter);
  m_error->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_error->hide();

  m_controls = new QToolBar(m_view->viewport());
  view->setOverlay(m_controls);
  m_controls->setObjectName(QStringLiteral("imageViewerControls"));
  m_controls->setIconSize(QSize(20, 20));
  m_controls->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_controls->setMovable(false);
  m_controls->setFloatable(false);
  m_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_controls->setStyleSheet(QStringLiteral("QToolBar#imageViewerControls { spacing: 4px; }"));
  const auto addControl = [this](const QString &p_name, const QString &p_label,
                                 const QString &p_icon) {
    auto *action = m_controls->addAction(p_label);
    action->setObjectName(p_name);
    action->setToolTip(p_label);
    action->setProperty("iconName", p_icon);
    auto *button = qobject_cast<QToolButton *>(m_controls->widgetForAction(action));
    button->setObjectName(p_name);
    button->setAccessibleName(p_label);
    button->setMinimumSize(32, 32);
    return action;
  };
  m_zoomIn = addControl(QStringLiteral("imageZoomIn"),
                        QCoreApplication::translate("vnotex::ImageViewWindow", "Zoom In"),
                        QStringLiteral("zoom_in_editor.svg"));
  m_zoomOut = addControl(QStringLiteral("imageZoomOut"),
                         QCoreApplication::translate("vnotex::ImageViewWindow", "Zoom Out"),
                         QStringLiteral("zoom_out_editor.svg"));
  m_reset =
      addControl(QStringLiteral("imageReset"),
                 QCoreApplication::translate("vnotex::ImageViewWindow", "Reset (fit and center)"),
                 QStringLiteral("reset_editor.svg"));
  connect(m_zoomIn, &QAction::triggered, this, [this]() { zoom(true); });
  connect(m_zoomOut, &QAction::triggered, this, [this]() { zoom(false); });
  connect(m_reset, &QAction::triggered, this, &ImageViewer::resetZoom);
  if (p_mode == Mode::Overlay) {
    m_close = new QToolButton(this);
    m_close->setObjectName(QStringLiteral("imageClose"));
    const auto label = QCoreApplication::translate("QPlatformTheme", "Close");
    m_close->setToolTip(label);
    m_close->setAccessibleName(label);
    m_close->setAutoRaise(true);
    m_close->setFixedSize(32, 32);
    m_close->setIconSize(QSize(20, 20));
    m_close->setFocusPolicy(Qt::StrongFocus);
    connect(m_close, &QToolButton::clicked, this, &ImageViewer::closeRequested);
    // Override the application's generic QWidget background only on the overlay surfaces.
    setStyleSheet(QStringLiteral("vnotex--ImageViewer, QGraphicsView, QGraphicsView > QWidget "
                                 "{ background: transparent; border: none; }"));
    // Paint the theme tint over the live editor, not over an opaque viewport.
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setAutoFillBackground(false);
    m_view->viewport()->setAutoFillBackground(false);
    m_view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    if (auto *theme = m_services.get<ThemeService>()) {
      connect(theme, &ThemeService::themeChanged, this, &ImageViewer::handleThemeChanged);
    }
  }
  m_view->viewport()->installEventFilter(this);
  m_view->installEventFilter(this);
  installEventFilter(this);
  handleThemeChanged();
  setImage(QImage());
}

void ImageViewer::setShortcutTips(const QKeySequence &p_zoomIn, const QKeySequence &p_zoomOut,
                                   const QKeySequence &p_reset) {
  const auto tip = [](const QString &p_label, const QKeySequence &p_key) {
    return p_key.isEmpty()
               ? p_label
               : QStringLiteral("%1\t%2").arg(p_label, p_key.toString(QKeySequence::NativeText));
  };
  m_zoomIn->setToolTip(tip(m_zoomIn->text(), p_zoomIn));
  m_zoomOut->setToolTip(tip(m_zoomOut->text(), p_zoomOut));
  m_reset->setToolTip(tip(m_reset->text(), p_reset));
}

void ImageViewer::setImage(const QImage &p_image) {
  cancelDrag();
  m_image->setPixmap(QPixmap());
  m_view->scene()->setSceneRect(QRectF());
  m_view->resetTransform();
  m_imageSize = QSizeF();
  m_scale = 1.0;
  m_fitMode = true;
  m_error->show();
  updateControls();
  positionOverlay();

  auto image = p_image;
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return;
  }
  image.setDevicePixelRatio(1.0);
  m_imageSize = image.size();
  m_image->setPixmap(QPixmap::fromImage(std::move(image)));
  if (m_image->pixmap().isNull()) {
    m_imageSize = QSizeF();
    return;
  }
  m_view->scene()->setSceneRect(m_image->boundingRect());
  m_error->hide();
  updateControls();
  scheduleFit();
}
qreal ImageViewer::fitScale() const {
  if (m_imageSize.isEmpty() || m_view->viewport()->size().isEmpty()) {
    return 1.0;
  }
  const auto size = m_view->viewport()->size();
  return qMin(qreal(1.0),
              qMin(size.width() / m_imageSize.width(), size.height() / m_imageSize.height()));
}

void ImageViewer::fitImage() {
  if (!m_fitMode || m_imageSize.isEmpty() || !m_view->isVisible() ||
      m_view->viewport()->size().isEmpty()) {
    return;
  }
  m_scale = fitScale();
  m_view->setTransform(QTransform::fromScale(m_scale, m_scale));
  m_view->centerOn(m_image->boundingRect().center());
  updateControls();
}

void ImageViewer::scheduleFit() {
  if (m_fitPending) {
    return;
  }
  m_fitPending = true;
  QTimer::singleShot(0, this, [this]() {
    m_fitPending = false;
    positionOverlay();
    fitImage();
    updateControls();
  });
}

void ImageViewer::zoom(bool p_zoomIn) {
  if (m_imageSize.isEmpty()) {
    return;
  }
  const auto center = m_view->mapToScene(m_view->viewport()->rect().center());
  m_fitMode = false;
  m_scale = qBound(qMin(qreal(0.05), fitScale()), p_zoomIn ? m_scale * 1.25 : m_scale / 1.25,
                   qreal(16.0));
  m_view->setTransform(QTransform::fromScale(m_scale, m_scale));
  // The scene rectangle bounds the hidden scrollbars; undersized axes stay centered.
  m_view->centerOn(center);
  updateControls();
}

void ImageViewer::resetZoom() {
  cancelDrag();
  m_fitMode = true;
  fitImage();
  scheduleFit();
}

void ImageViewer::updateControls() {
  const bool loaded = !m_imageSize.isEmpty();
  m_zoomIn->setEnabled(loaded && m_scale < 16.0);
  m_zoomOut->setEnabled(loaded && m_scale > qMin(qreal(0.05), fitScale()));
  m_reset->setEnabled(loaded);
}

void ImageViewer::positionOverlay() {
  const auto size = m_view->viewport()->size();
  m_error->setGeometry(m_view->viewport()->rect());
  m_controls->adjustSize();
  m_controls->move(qMax(0, size.width() - m_controls->width() - 12),
                   qMax(0, size.height() - m_controls->height() - 12));
  m_controls->raise();
  if (m_close) {
    m_close->move(qMax(0, width() - m_close->width() - 12), 12);
    m_close->raise();
  }
}

void ImageViewer::cancelDrag() {
  // Reset Qt's internal hand-scroll state as well as any outstanding mouse grab.
  m_view->setDragMode(QGraphicsView::NoDrag);
  if (QWidget::mouseGrabber() == m_view->viewport()) {
    m_view->viewport()->releaseMouse();
  }
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
}

bool ImageViewer::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (m_close) {
    auto *widget = qobject_cast<QWidget *>(p_obj);
    const bool keyEvent = p_event->type() == QEvent::ShortcutOverride ||
                          p_event->type() == QEvent::KeyPress ||
                          p_event->type() == QEvent::KeyRelease;
    if (!widget && keyEvent && p_obj == window()->windowHandle()) {
      // Native keys reach QWindow before QWidget; claim them before presentation's filter.
      widget = QApplication::focusWidget();
    }
    if (!widget || (widget != this && !isAncestorOf(widget))) {
      return false;
    }
    if (p_obj == this && p_event->type() == QEvent::Show) {
      // Run before the presentation host's Escape filter, even after being shown again.
      qApp->installEventFilter(this);
    }
    if (isVisible()) {
      if (p_event->type() == QEvent::ShortcutOverride) {
        // The overlay shares the editor's window; do not invoke its editing shortcuts.
        p_event->accept();
        return true;
      }
      if (p_event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(p_event);
        p_event->accept();
        if (key->key() == Qt::Key_Escape) {
          emit closeRequested();
          return true;
        }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
          const std::array<QWidget *, 4> controls{{m_close, m_controls->widgetForAction(m_zoomIn),
                                                   m_controls->widgetForAction(m_zoomOut),
                                                   m_controls->widgetForAction(m_reset)}};
          const bool backward =
              key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
          int index = backward ? 0 : -1;
          for (int i = 0; i < 4; ++i) {
            if (controls[i]->hasFocus()) {
              index = i;
              break;
            }
          }
          for (int i = 0; i < 4; ++i) {
            index = (index + (backward ? 3 : 1)) % 4;
            if (controls[index]->isEnabled()) {
              controls[index]->setFocus(backward ? Qt::BacktabFocusReason : Qt::TabFocusReason);
              break;
            }
          }
          return true;
        }
        if (!key->isAutoRepeat() && (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return ||
                                     key->key() == Qt::Key_Enter)) {
          if (auto *button = qobject_cast<QToolButton *>(widget)) {
            button->click();
          }
        }
        return true;
      }
      if (p_event->type() == QEvent::KeyRelease || p_event->type() == QEvent::ContextMenu) {
        p_event->accept();
        return true;
      }
      if (p_event->type() == QEvent::Wheel) {
        const auto *wheel = static_cast<QWheelEvent *>(p_event);
        if (p_obj == m_view->viewport() && wheel->modifiers() == Qt::NoModifier &&
            wheel->angleDelta().y() &&
            m_image->contains(
                m_image->mapFromScene(m_view->mapToScene(wheel->position().toPoint())))) {
          zoom(wheel->angleDelta().y() > 0);
        }
        // In particular, Ctrl+wheel must not bubble up and zoom the mind map.
        p_event->accept();
        return true;
      }
      if (p_obj == m_view->viewport()) {
        if (p_event->type() == QEvent::MouseButtonPress) {
          const auto *mouse = static_cast<QMouseEvent *>(p_event);
          m_backdropPressed =
              mouse->button() == Qt::LeftButton &&
              !m_image->contains(m_image->mapFromScene(m_view->mapToScene(mouse->pos())));
          m_backdropPress = mouse->pos();
          if (m_backdropPressed) {
            p_event->accept();
            return true;
          }
        } else if (p_event->type() == QEvent::MouseMove && m_backdropPressed) {
          const auto *mouse = static_cast<QMouseEvent *>(p_event);
          if ((mouse->pos() - m_backdropPress).manhattanLength() >
              QApplication::startDragDistance()) {
            m_backdropPressed = false;
          }
        } else if (p_event->type() == QEvent::MouseButtonRelease && m_backdropPressed) {
          const auto *mouse = static_cast<QMouseEvent *>(p_event);
          if (mouse->button() == Qt::LeftButton) {
            m_backdropPressed = false;
            const bool outside =
                !m_image->contains(m_image->mapFromScene(m_view->mapToScene(mouse->pos())));
            p_event->accept();
            if (outside) {
              emit closeRequested();
            }
            return true;
          }
        }
      }
    }
  }
  switch (p_event->type()) {
  case QEvent::Show:
  case QEvent::Resize:
    if (p_obj == m_view->viewport()) {
      positionOverlay();
      scheduleFit();
    }
    break;
  case QEvent::Hide:
  case QEvent::WindowDeactivate:
  case QEvent::FocusOut:
    cancelDrag();
    break;
  default:
    break;
  }
  return QWidget::eventFilter(p_obj, p_event);
}

void ImageViewer::scrollUp() {
  m_view->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);
}

void ImageViewer::scrollDown() {
  m_view->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
}

void ImageViewer::handleThemeChanged() {
  auto colors = palette();
  auto *theme = m_services.get<ThemeService>();
  if (theme) {
    const QVector<IconUtils::OverriddenColor> iconColors{
        {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#fg")), QIcon::Normal},
        {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg")), QIcon::Disabled}};
    for (auto *action : m_controls->actions()) {
      action->setIcon(IconUtils::fetchIcon(
          theme->getIconFile(action->property("iconName").toString()), iconColors));
    }
    if (m_close) {
      m_close->setIcon(
          IconUtils::fetchIcon(theme->getIconFile(QStringLiteral("close.svg")), iconColors));
    }
    const QColor background(theme->paletteColor(QStringLiteral("base#content#bg")));
    if (background.isValid()) {
      colors.setColor(QPalette::Base, background);
      colors.setColor(QPalette::Window, background);
    }
  }
  if (!theme) {
    for (auto *action : m_controls->actions()) {
      action->setIcon(QIcon());
    }
  }
  colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
  m_view->setPalette(colors);
  m_view->viewport()->setPalette(colors);
  m_error->setPalette(colors);
  if (m_close) {
    m_close->setPalette(colors);
    if (!theme) {
      m_close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    }
    auto background = colors.color(QPalette::Base);
    background.setAlphaF(0.9);
    m_view->setBackgroundBrush(background);
  }
}
