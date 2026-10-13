#ifndef IMAGEVIEWWINDOW_H
#define IMAGEVIEWWINDOW_H

#include "viewwindow2.h"

#include <QSizeF>

class QAction;
class QGraphicsPixmapItem;
class QGraphicsView;
class QLabel;
class QToolBar;

namespace vnotex {

// Read-only, first-frame image viewer. The camera never modifies the buffer.
class ImageViewWindow : public ViewWindow2 {
  Q_OBJECT

public:
  explicit ImageViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                           QWidget *p_parent = nullptr);
  QString getLatestContent() const override;
  void setMode(ViewWindowMode p_mode) override;
  bool isSaveSupported() const override { return false; }
  void handleThemeChanged() override;

protected:
  void syncEditorFromBuffer() override;
  void setModified(bool p_modified) override;
  void scrollUp() override;
  void scrollDown() override;
  void zoom(bool p_zoomIn) override;
  void resetZoom() override;
  bool isPrintSupported() const override { return false; }
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

private:
  qreal fitScale() const;
  void fitImage();
  void scheduleFit();
  void updateControls();
  void positionOverlay();
  void cancelDrag();

  QGraphicsView *m_view = nullptr;
  QGraphicsPixmapItem *m_image = nullptr;
  QLabel *m_error = nullptr;
  QToolBar *m_controls = nullptr;
  QAction *m_zoomIn = nullptr;
  QAction *m_zoomOut = nullptr;
  QAction *m_reset = nullptr;
  QSizeF m_imageSize;
  qreal m_scale = 1.0;
  bool m_fitMode = true;
  bool m_fitPending = false;
};

} // namespace vnotex

#endif // IMAGEVIEWWINDOW_H
