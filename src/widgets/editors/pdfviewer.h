#ifndef PDFVIEWER_H
#define PDFVIEWER_H

#include "../webviewer.h"

namespace vnotex
{
    class PdfViewerAdapter;

    class PdfViewer : public WebViewer
    {
        Q_OBJECT
    public:
        PdfViewer(PdfViewerAdapter *p_adapter,
                  const QColor &p_background,
                  qreal p_zoomFactor,
                  QWidget *p_parent = nullptr);

        PdfViewerAdapter *adapter() const;

    private:
        // Managed by QObject.
        PdfViewerAdapter *m_adapter = nullptr;
    };
}

#endif // PDFVIEWER_H
