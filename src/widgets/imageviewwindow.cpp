#include "imageviewwindow.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

#include <limits>

#include "imageviewer.h"

using namespace vnotex;

ImageViewWindow::ImageViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  setLayoutMode(ViewWindowLayoutMode::FullWidth);
  m_viewer = new ImageViewer(p_services, this);
  setCentralWidget(m_viewer);
  setFocusProxy(m_viewer);
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
  m_viewer->setImage(QImage());

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
  m_viewer->setImage(reader.read());
}

void ImageViewWindow::scrollUp() { m_viewer->scrollUp(); }

void ImageViewWindow::scrollDown() { m_viewer->scrollDown(); }

void ImageViewWindow::zoom(bool p_zoomIn) { m_viewer->zoom(p_zoomIn); }

void ImageViewWindow::resetZoom() { m_viewer->resetZoom(); }

void ImageViewWindow::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();
  m_viewer->handleThemeChanged();
}
