#ifndef IMAGEVIEWWINDOW_H
#define IMAGEVIEWWINDOW_H

#include "viewwindow2.h"

namespace vnotex {
class ImageViewer;

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

private:
  ImageViewer *m_viewer = nullptr;
};

} // namespace vnotex

#endif // IMAGEVIEWWINDOW_H
