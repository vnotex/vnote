#include "pdfvieweradapter.h"

using namespace vnotex;

PdfViewerAdapter::PdfViewerAdapter(QObject *p_parent)
    : WebViewAdapter(p_parent)
{
}

void PdfViewerAdapter::setUrl(const QString &p_url)
{
    // TODO: Not supported yet.
    Q_ASSERT(false);
    if (isReady()) {
        emit urlUpdated(p_url);
    } else {
        pendAction(std::bind(&PdfViewerAdapter::setUrl, this, p_url));
    }
}
