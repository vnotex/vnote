#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QPoint>
#include <QSizeF>
#include <QWidget>

class QAction;
class QGraphicsPixmapItem;
class QGraphicsView;
class QImage;
class QKeySequence;
class QLabel;
class QToolBar;
class QToolButton;

namespace vnotex {
class ServiceLocator;

// Shared read-only camera for already-decoded images.
class ImageViewer : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Embedded, Overlay };
  explicit ImageViewer(ServiceLocator &p_services, QWidget *p_parent = nullptr,
                       Mode p_mode = Mode::Embedded);
  void setImage(const QImage &p_image);
  void zoom(bool p_zoomIn);
  void resetZoom();
  void scrollUp();
  void scrollDown();
  void handleThemeChanged();

  // Display only; empty sequences remove tips. The caller owns shortcut bindings.
  void setShortcutTips(const QKeySequence &p_zoomIn, const QKeySequence &p_zoomOut,
                       const QKeySequence &p_reset);

signals:
  void closeRequested();

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

private:
  qreal fitScale() const;
  void fitImage();
  void scheduleFit();
  void updateControls();
  void positionOverlay();
  void cancelDrag();

  ServiceLocator &m_services;
  QGraphicsView *m_view = nullptr;
  QGraphicsPixmapItem *m_image = nullptr;
  QLabel *m_error = nullptr;
  QToolBar *m_controls = nullptr;
  QAction *m_zoomIn = nullptr;
  QAction *m_zoomOut = nullptr;
  QAction *m_reset = nullptr;
  QToolButton *m_close = nullptr;
  QPoint m_backdropPress;
  bool m_backdropPressed = false;
  QSizeF m_imageSize;
  qreal m_scale = 1.0;
  bool m_fitMode = true;
  bool m_fitPending = false;
};

} // namespace vnotex

#endif // IMAGEVIEWER_H
