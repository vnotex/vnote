#include "pdfvieweradapter.h"

using namespace vnotex;

PdfViewerAdapter::PdfViewerAdapter(QObject *p_parent)
    : QObject(p_parent)
{
}

void PdfViewerAdapter::setUrl(const QString &p_url)
{
    // TODO: Not supported yet.
    Q_ASSERT(false);
    if (m_viewerReady) {
        emit urlUpdated(p_url);
    } else {
        m_pendingActions.append([this, p_url]() {
            emit urlUpdated(p_url);
        });
    }
}

void PdfViewerAdapter::setReady(bool p_ready)
{
    if (m_viewerReady == p_ready) {
        return;
    }

    m_viewerReady = p_ready;
    if (m_viewerReady) {
        for (auto &act : m_pendingActions) {
            act();
        }
        m_pendingActions.clear();
    }
}
