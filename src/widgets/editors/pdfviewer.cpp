#include "pdfviewer.h"

#include <QWebChannel>

#include "pdfvieweradapter.h"

using namespace vnotex;

PdfViewer::PdfViewer(PdfViewerAdapter *p_adapter, const QColor &p_background, qreal p_zoomFactor,
                     QWidget *p_parent, QWebEngineProfile *p_profile)
    : WebViewer(p_background, p_zoomFactor, p_parent, p_profile), m_adapter(p_adapter) {
  m_adapter->setParent(this);

  auto channel = new QWebChannel(this);
  channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

  page()->setWebChannel(channel);
}

PdfViewerAdapter *PdfViewer::adapter() const { return m_adapter; }
