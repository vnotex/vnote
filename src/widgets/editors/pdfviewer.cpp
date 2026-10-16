#include "pdfviewer.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScopedPointer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>

#include <core/services/commenttypes.h>
#include <core/vxpdfscheme.h>

#include "../webpage.h"
#include "pdfvieweradapter.h"

using namespace vnotex;

PdfViewer::PdfViewer(PdfViewerAdapter *p_adapter, const QColor &p_background, qreal p_zoomFactor,
                     QWidget *p_parent, QWebEngineProfile *p_profile,
                     CommentColorSwatch::ColorResolver p_resolve, QString p_borderCss)
    : WebViewer(p_background, p_zoomFactor, p_parent, p_profile), m_adapter(p_adapter),
      m_resolve(std::move(p_resolve)), m_borderCss(std::move(p_borderCss)) {
  m_adapter->setParent(this);

  auto channel = new QWebChannel(this);
  channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

  page()->setWebChannel(channel);

  // Allow ONLY the viewer route of the vxpdf scheme in the main frame. Assets and
  // document bytes are subresources and never reach acceptNavigationRequest;
  // everything else (vx://home, vx://settings, user-authored links) keeps flowing
  // through externalLinkRequested.
  if (auto *webPage = qobject_cast<WebPage *>(page())) {
    webPage->setAllowedMainFrameUrlPredicate([](const QUrl &p_url) {
      return p_url.scheme() == VxPdfScheme::scheme() && p_url.host() == VxPdfScheme::host() &&
             p_url.path() ==
                 VxPdfScheme::assetPathPrefix() + VxPdfScheme::viewerTemplateRelativePath();
    });
  }
}

PdfViewerAdapter *PdfViewer::adapter() const { return m_adapter; }

void PdfViewer::setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve,
                                  QString p_borderCss) {
  m_resolve = std::move(p_resolve);
  m_borderCss = std::move(p_borderCss);
}

void PdfViewer::contextMenuEvent(QContextMenuEvent *p_event) {
  QScopedPointer<QMenu> menu(createStandardContextMenu());
  if (!menu) {
    menu.reset(new QMenu(this));
  }

  if (page()->hasSelection()) {
    menu->addSeparator();

    auto *highlight = menu->addMenu(tr("Highlight"));
    // Driven by the schema, NOT by a hand-written list: a token added to
    // CommentColor::all() must show up here and in the comment dock together,
    // and both take their label from CommentColor::displayName().
    for (const auto &token : CommentColor::all()) {
      auto *act = highlight->addAction(CommentColorSwatch::icon(m_resolve, token, 16, m_borderCss),
                                       CommentColor::displayName(token));
      connect(act, &QAction::triggered, this,
              [this, token]() { emit highlightSelectionRequested(token); });
    }
  }

  if (menu->isEmpty()) {
    return;
  }

  menu->exec(p_event->globalPos());
}
