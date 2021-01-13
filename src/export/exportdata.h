#ifndef EXPORTDATA_H
#define EXPORTDATA_H

#include <QSharedPointer>

class QPageLayout;

namespace vnotex
{
    enum class ExportSource
    {
        CurrentBuffer = 0,
        CurrentFolder,
        CurrentNotebook
    };

    enum class ExportFormat
    {
        Markdown = 0,
        HTML,
        PDF,
        Custom
    };

    struct ExportHtmlOption
    {
        QJsonObject toJson() const;
        void fromJson(const QJsonObject &p_obj);

        bool operator==(const ExportHtmlOption &p_other) const;

        bool m_embedStyles = true;

        bool m_completePage = true;

        bool m_embedImages = true;

        bool m_useMimeHtmlFormat = false;

        // Whether add outline panel.
        bool m_addOutlinePanel = true;
    };

    struct ExportPdfOption
    {
        ExportPdfOption();

        QJsonObject toJson() const;
        void fromJson(const QJsonObject &p_obj);

        bool operator==(const ExportPdfOption &p_other) const;

        QSharedPointer<QPageLayout> m_layout;

        // Add TOC at the front to complement the missing of outline.
        bool m_addTableOfContents = false;

        bool m_useWkhtmltopdf = false;

        QString m_wkhtmltopdfExePath;

        QString m_wkhtmltopdfArgs;
    };

    struct ExportOption
    {
        QJsonObject toJson() const;
        void fromJson(const QJsonObject &p_obj);

        bool operator==(const ExportOption &p_other) const;

        ExportSource m_source = ExportSource::CurrentBuffer;

        ExportFormat m_targetFormat = ExportFormat::HTML;

        bool m_useTransparentBg = true;

        QString m_renderingStyleFile;

        QString m_syntaxHighlightStyleFile;

        QString m_outputDir;

        bool m_recursive = true;

        bool m_exportAttachments = true;

        ExportHtmlOption m_htmlOption;

        ExportPdfOption m_pdfOption;
    };

    inline QString exportFormatString(ExportFormat p_format)
    {
        switch (p_format)
        {
        case ExportFormat::Markdown:
            return QStringLiteral("Markdown");

        case ExportFormat::HTML:
            return QStringLiteral("HTML");

        case ExportFormat::PDF:
            return QStringLiteral("PDF");

        case ExportFormat::Custom:
            return QStringLiteral("Custom");
        }

        return QStringLiteral("Unknown");
    }
}

#endif // EXPORTDATA_H
