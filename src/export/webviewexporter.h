#ifndef WEBVIEWEXPORTER_H
#define WEBVIEWEXPORTER_H

#include <QObject>

#include "exportdata.h"

class QWidget;

namespace vnotex
{
    class File;
    class MarkdownViewer;

    class WebViewExporter : public QObject
    {
        Q_OBJECT
    public:
        enum WebViewState
        {
            Started = 0,
            LoadFinished = 0x1,
            WorkFinished = 0x2,
            Failed = 0x4
        };
        Q_DECLARE_FLAGS(WebViewStates, WebViewState);

        // We need QWidget as parent.
        explicit WebViewExporter(QWidget *p_parent);

        ~WebViewExporter();

        bool doExport(const ExportOption &p_option,
                      const File *p_file,
                      const QString &p_outputFile);

        void prepare(const ExportOption &p_option);

        // Release resources after one batch of export.
        void clear();

        void stop();

    signals:
        void logRequested(const QString &p_log);

    private:
        enum class ExportState
        {
            Busy = 0,
            Finished,
            Failed
        };

        bool isWebViewReady() const;

        bool isWebViewFailed() const;

        bool doExportHtml(const ExportHtmlOption &p_htmlOption,
                          const QString &p_outputFile,
                          const QUrl &p_baseUrl);

        bool writeHtmlFile(const QString &p_file,
                           const QUrl &p_baseUrl,
                           const QString &p_headContent,
                           QString p_styleContent,
                           const QString &p_content,
                           const QString &p_bodyClassList,
                           bool p_embedStyles,
                           bool p_completePage,
                           bool p_embedImages);

        bool embedStyleResources(QString &p_html) const;

        bool embedBodyResources(const QUrl &p_baseUrl, QString &p_html);

        bool fixBodyResources(const QUrl &p_baseUrl, const QString &p_folder, QString &p_html);

        bool doExportPdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile);

        bool doExportWkhtmltopdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile, const QUrl &p_baseUrl);

        QSize pageLayoutSize(const QPageLayout &p_layout) const;

        bool doWkhtmltopdf(const ExportPdfOption &p_pdfOption, const QStringList &p_htmlFiles, const QString &p_outputFile);

        void prepareWkhtmltopdfArguments(const ExportPdfOption &p_pdfOption);

        bool startProcess(const QString &p_program, const QStringList &p_args);

        bool m_askedToStop = false;

        bool m_exportOngoing = false;

        WebViewStates m_webViewStates = WebViewState::Started;

        // Managed by QObject.
        MarkdownViewer *m_viewer = nullptr;

        QString m_htmlTemplate;

        QString m_exportHtmlTemplate;

        QStringList m_wkhtmltopdfArgs;
    };
}

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::WebViewExporter::WebViewStates)

#endif // WEBVIEWEXPORTER_H
