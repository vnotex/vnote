#ifndef PDFVIEWERADAPTER_H
#define PDFVIEWERADAPTER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QScopedPointer>
#include <QJsonArray>
#include <QTextCharFormat>

#include <core/global.h>
#include <utils/callbackpool.h>

namespace vnotex
{
    // Adapter and interface between CPP and JS for PDF.
    class PdfViewerAdapter : public QObject
    {
        Q_OBJECT
    public:
        explicit PdfViewerAdapter(QObject *p_parent = nullptr);

        virtual ~PdfViewerAdapter();

        // @p_lineNumber: the line number needed to sync, -1 for invalid.
        void setText(int p_revision,
                     const QString &p_text,
                     int p_lineNumber);

        // @p_lineNumber: the line number needed to sync, -1 for invalid.
        void setText(const QString &p_text, int p_lineNumber = -1);

    signals:
        // Current Markdown text is updated.
        void textUpdated(const QString &p_text);
    };
}

#endif // PDFVIEWERADAPTER_H
