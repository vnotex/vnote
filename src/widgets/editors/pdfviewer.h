#ifndef PDFVIEWER_H
#define PDFVIEWER_H

#include "../webviewer.h"

#include <gui/utils/commentcolorswatch.h>

class QContextMenuEvent;

namespace vnotex {
class PdfViewerAdapter;

class PdfViewer : public WebViewer {
  Q_OBJECT
public:
  // Takes the swatch COLOUR RESOLVER, not a ThemeService: PdfViewer has no
  // ServiceLocator, and either would put a ThemeService symbol reference into
  // pdfviewer.cpp. PdfViewWindow2 supplies both at the single construction
  // site, where it has already resolved the service.
  PdfViewer(PdfViewerAdapter *p_adapter, const QColor &p_background, qreal p_zoomFactor,
            QWidget *p_parent = nullptr, QWebEngineProfile *p_profile = nullptr,
            CommentColorSwatch::ColorResolver p_resolve = {}, QString p_borderCss = QString());

  PdfViewerAdapter *adapter() const;

  // Theme switch. The context menu itself needs no rebuild — it is recreated
  // per event — so it picks up whatever is held here at the time.
  void setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve, QString p_borderCss);

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

  CommentColorSwatch::ColorResolver m_resolve;

  QString m_borderCss;
};
} // namespace vnotex

#endif // PDFVIEWER_H
