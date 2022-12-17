#ifndef PDFVIEWERADAPTER_H
#define PDFVIEWERADAPTER_H

#include <QObject>
#include <QString>
#include <QJsonObject>

#include <core/global.h>

namespace vnotex
{
    // Adapter and interface between CPP and JS for PDF.
    class PdfViewerAdapter : public QObject
    {
        Q_OBJECT
    public:
        explicit PdfViewerAdapter(QObject *p_parent = nullptr);

        ~PdfViewerAdapter() = default;

        void setUrl(const QString &p_url);

        // Functions to be called from web side.
    public slots:
        void setReady(bool p_ready);

        // Signals to be connected at web side.
    signals:
        void urlUpdated(const QString &p_url);

    private:
        // Whether web side viewer is ready to handle url update.
        bool m_viewerReady = false;

        // Pending actions for the viewer once it is ready.
        QVector<std::function<void()>> m_pendingActions;
    };
}

#endif // PDFVIEWERADAPTER_H
