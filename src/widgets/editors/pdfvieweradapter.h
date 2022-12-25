#ifndef PDFVIEWERADAPTER_H
#define PDFVIEWERADAPTER_H

#include "webviewadapter.h"

namespace vnotex
{
    // Adapter and interface between CPP and JS for PDF.
    class PdfViewerAdapter : public WebViewAdapter
    {
        Q_OBJECT
    public:
        explicit PdfViewerAdapter(QObject *p_parent = nullptr);

        ~PdfViewerAdapter() = default;

        void setUrl(const QString &p_url);

        // Functions to be called from web side.
    public slots:

        // Signals to be connected at web side.
    signals:
        void urlUpdated(const QString &p_url);
    };
}

#endif // PDFVIEWERADAPTER_H
