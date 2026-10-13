#include "imageviewwindow.h"

#include <QAction>
#include <QBuffer>
#include <QEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImageReader>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <limits>
#include <utility>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>

#include "viewwindowtoolbarhelper2.h"

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

ImageViewWindow::ImageViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  setLayoutMode(ViewWindowLayoutMode::FullWidth);
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
  setCentralWidget(m_view);
  setFocusProxy(m_view);

  m_error = new QLabel(tr("Unable to display image"), m_view->viewport());
  m_error->setTextFormat(Qt::PlainText);
  m_error->setAlignment(Qt::AlignCenter);
  m_error->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_error->hide();

  m_controls = createToolBar(m_view->viewport());
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
    auto *action = m_controls->addAction(
        ViewWindowToolBarHelper2::generateIcon(getServices(), p_icon), p_label);
    action->setObjectName(p_name);
    action->setToolTip(p_label);
    action->setProperty("iconName", p_icon);
    auto *button = qobject_cast<QToolButton *>(m_controls->widgetForAction(action));
    button->setObjectName(p_name);
    button->setAccessibleName(p_label);
    button->setMinimumSize(32, 32);
    return action;
  };
  m_zoomIn = addControl(QStringLiteral("imageZoomIn"), tr("Zoom In"),
                        QStringLiteral("zoom_in_editor.svg"));
  m_zoomOut = addControl(QStringLiteral("imageZoomOut"), tr("Zoom Out"),
                         QStringLiteral("zoom_out_editor.svg"));
  m_reset = addControl(QStringLiteral("imageReset"), tr("Reset (fit and center)"),
                       QStringLiteral("reset_editor.svg"));
  connect(m_zoomIn, &QAction::triggered, this, [this]() { zoom(true); });
  connect(m_zoomOut, &QAction::triggered, this, [this]() { zoom(false); });
  connect(m_reset, &QAction::triggered, this, &ImageViewWindow::resetZoom);
  m_view->viewport()->installEventFilter(this);
  m_view->installEventFilter(this);
  installEventFilter(this);
  handleThemeChanged();
  syncEditorFromBuffer();
}

QString ImageViewWindow::getLatestContent() const { return QString(); }

void ImageViewWindow::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  m_mode = ViewWindowMode::Read;
}

void ImageViewWindow::setModified(bool p_modified) { Q_UNUSED(p_modified); }

void ImageViewWindow::syncEditorFromBuffer() {
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

  if (!getBuffer().isValid()) {
    return;
  }
  VxCoreError error = VXCORE_OK;
  const auto raw = getBuffer().peekContentRaw(&error);
  if (error != VXCORE_OK || raw.isEmpty() || raw.size() > std::numeric_limits<int>::max()) {
    return;
  }
  // Both the borrowed QByteArray and its reader die before any buffer mutation.
  auto bytes = QByteArray::fromRawData(raw.data(), static_cast<int>(raw.size()));
  QBuffer device(&bytes);
  device.open(QIODevice::ReadOnly);
  QImageReader reader(&device);
  reader.setAutoTransform(true);
  auto image = reader.read();
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

qreal ImageViewWindow::fitScale() const {
  if (m_imageSize.isEmpty() || m_view->viewport()->size().isEmpty()) {
    return 1.0;
  }
  const auto size = m_view->viewport()->size();
  return qMin(qreal(1.0),
              qMin(size.width() / m_imageSize.width(), size.height() / m_imageSize.height()));
}

void ImageViewWindow::fitImage() {
  if (!m_fitMode || m_imageSize.isEmpty() || !m_view->isVisible() ||
      m_view->viewport()->size().isEmpty()) {
    return;
  }
  m_scale = fitScale();
  m_view->setTransform(QTransform::fromScale(m_scale, m_scale));
  m_view->centerOn(m_image->boundingRect().center());
  updateControls();
}

void ImageViewWindow::scheduleFit() {
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

void ImageViewWindow::zoom(bool p_zoomIn) {
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

void ImageViewWindow::resetZoom() {
  cancelDrag();
  m_fitMode = true;
  fitImage();
  scheduleFit();
}

void ImageViewWindow::updateControls() {
  const bool loaded = !m_imageSize.isEmpty();
  m_zoomIn->setEnabled(loaded && m_scale < 16.0);
  m_zoomOut->setEnabled(loaded && m_scale > qMin(qreal(0.05), fitScale()));
  m_reset->setEnabled(loaded);
}

void ImageViewWindow::positionOverlay() {
  const auto size = m_view->viewport()->size();
  m_error->setGeometry(m_view->viewport()->rect());
  m_controls->adjustSize();
  m_controls->move(qMax(0, size.width() - m_controls->width() - 12),
                   qMax(0, size.height() - m_controls->height() - 12));
  m_controls->raise();
}

void ImageViewWindow::cancelDrag() {
  // Reset Qt's internal hand-scroll state as well as any outstanding mouse grab.
  m_view->setDragMode(QGraphicsView::NoDrag);
  if (QWidget::mouseGrabber() == m_view->viewport()) {
    m_view->viewport()->releaseMouse();
  }
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
}

bool ImageViewWindow::eventFilter(QObject *p_obj, QEvent *p_event) {
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
  return ViewWindow2::eventFilter(p_obj, p_event);
}

void ImageViewWindow::scrollUp() {
  m_view->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);
}

void ImageViewWindow::scrollDown() {
  m_view->verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
}

void ImageViewWindow::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();
  ViewWindowToolBarHelper2::refreshToolBarIcons(m_controls, getServices());
  auto colors = palette();
  if (auto *theme = getServices().get<ThemeService>()) {
    const QColor background(theme->paletteColor(QStringLiteral("base#content#bg")));
    if (background.isValid()) {
      colors.setColor(QPalette::Base, background);
      colors.setColor(QPalette::Window, background);
    }
  }
  colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
  m_view->setPalette(colors);
  m_view->viewport()->setPalette(colors);
  m_error->setPalette(colors);
}
