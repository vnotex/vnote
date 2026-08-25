#ifndef PDFVIEWER_H
#define PDFVIEWER_H

#include "../webviewer.h"

class QContextMenuEvent;

namespace vnotex {
class PdfViewerAdapter;

class PdfViewer : public WebViewer {
  Q_OBJECT
public:
  PdfViewer(PdfViewerAdapter *p_adapter, const QColor &p_background, qreal p_zoomFactor,
            QWidget *p_parent = nullptr, QWebEngineProfile *p_profile = nullptr);

  PdfViewerAdapter *adapter() const;

signals:
  // The user asked to highlight the current selection from the page context
  // menu. A VIEW emits an intent; PdfViewWindow2 routes it to the adapter.
  void highlightSelectionRequested(const QString &p_color);

protected:
  // Adds a "Highlight" submenu when the page has a text selection.
  //
  // This is the DISCOVERABLE entry point for creating a comment. Alt+drag
  // (bound in pdfviewer.mjs) is only a shortcut: a feature reachable solely by
  // a modifier key the user has to guess is not reachable at all.
  void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

private:
  // Managed by QObject.
  PdfViewerAdapter *m_adapter = nullptr;
};
} // namespace vnotex

#endif // PDFVIEWER_H
