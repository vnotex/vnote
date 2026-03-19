#ifndef PDFVIEWWINDOW2_H
#define PDFVIEWWINDOW2_H

#include "viewwindow2.h"

namespace vnotex {

class PdfViewer;
class PdfViewerAdapter;
class PdfViewWindowController;

// Concrete ViewWindow2 subclass for PDF files.
// Provides a read-only PDF viewer (PdfViewer via WebEngine + PDF.js)
// in the new architecture (ServiceLocator + Buffer2).
//
// This is a read-only window (ViewWindowMode::Read).
// Mode switching is not supported for PDF files.
class PdfViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  explicit PdfViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                          QWidget *p_parent = nullptr);

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

public slots:
  void handleEditorConfigChange() Q_DECL_OVERRIDE;

protected slots:
  void setModified(bool p_modified) Q_DECL_OVERRIDE;

protected:
  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void setupViewer();

  PdfViewerAdapter *adapter() const;

  // Managed by QObject parent (this).
  PdfViewWindowController *m_controller = nullptr;

  // Managed by QObject.
  PdfViewer *m_viewer = nullptr;
};

} // namespace vnotex

#endif // PDFVIEWWINDOW2_H
