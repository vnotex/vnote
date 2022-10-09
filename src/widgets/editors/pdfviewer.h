#ifndef PDFVIEWER_H
#define PDFVIEWER_H

#include "../webviewer.h"

namespace vnotex
{
    class PdfViewerAdapter;
    class PreviewHelper;
    class ViewWindow;

    class PdfViewer : public WebViewer
    {
        Q_OBJECT
    public:
        PdfViewer(PdfViewerAdapter *p_adapter,
                       const QColor &p_background,
                       qreal p_zoomFactor,
                       QWidget *p_parent = nullptr);

        PdfViewer(PdfViewerAdapter *p_adapter,
                       const ViewWindow *p_viewWindow,
                       const QColor &p_background,
                       qreal p_zoomFactor,
                       QWidget *p_parent = nullptr);

        PdfViewerAdapter *adapter() const;

    protected:
        void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

    private:
        // Managed by QObject.
        PdfViewerAdapter *m_adapter = nullptr;

        // Nullable.
        const ViewWindow *m_viewWindow = nullptr;
    };
}

#endif // PDFVIEWER_H
