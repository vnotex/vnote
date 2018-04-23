#ifndef VEXPORTER_H
#define VEXPORTER_H

#include <QObject>
#include <QPageLayout>
#include <QUrl>
#include <QWebEngineDownloadItem>
#include <QStringList>

#include "dialog/vexportdialog.h"

class QWidget;
class VWebView;
class VDocument;

class VExporter : public QObject
{
    Q_OBJECT
public:
    explicit VExporter(QWidget *p_parent = nullptr);

    void prepareExport(const ExportOption &p_opt);

    bool exportPDF(VFile *p_file,
                   const ExportOption &p_opt,
                   const QString &p_outputFile,
                   QString *p_errMsg = NULL);

    bool exportHTML(VFile *p_file,
                    const ExportOption &p_opt,
                    const QString &p_outputFile,
                    QString *p_errMsg = NULL);

    bool exportCustom(VFile *p_file,
                      const ExportOption &p_opt,
                      const QString &p_outputFile,
                      QString *p_errMsg = NULL);

    int exportPDFInOne(const QList<QString> &p_htmlFiles,
                       const ExportOption &p_opt,
                       const QString &p_outputFile,
                       QString *p_errMsg = NULL);

    int exportCustomInOne(const QList<QString> &p_files,
                          const ExportOption &p_opt,
                          const QString &p_outputFile,
                          QString *p_errMsg = NULL);

    void setAskedToStop(bool p_askedToStop);

signals:
    // Request to output log.
    void outputLog(const QString &p_log);

private slots:
    void handleLogicsFinished();

    void handleLoadFinished(bool p_ok);

    void handleDownloadRequested(QWebEngineDownloadItem *p_item);

private:
    enum class ExportState
    {
        Idle = 0,
        Cancelled,
        Busy,
        Failed,
        Successful
    };


    enum NoteState
    {
        NotReady = 0,
        WebLogicsReady = 0x1,
        WebLoadFinished = 0x2,
        Ready = 0x3,
        Failed = 0x4
    };


    void initWebViewer(VFile *p_file, const ExportOption &p_opt);

    void clearWebViewer();

    void clearNoteState();

    bool isNoteStateReady() const;

    bool isNoteStateFailed() const;

    bool exportViaWebView(VFile *p_file,
                          const ExportOption &p_opt,
                          const QString &p_outputFile,
                          QString *p_errMsg = NULL);

    bool exportToPDF(VWebView *p_webViewer,
                     const QString &p_filePath,
                     const QPageLayout &p_layout);

    bool exportToPDFViaWK(VDocument *p_webDocument,
                          const ExportPDFOption &p_opt,
                          const QString &p_filePath,
                          QString *p_errMsg = NULL);

    bool exportToCustom(VDocument *p_webDocument,
                        const ExportCustomOption &p_opt,
                        const QString &p_filePath,
                        QString *p_errMsg = NULL);

    bool exportToHTML(VDocument *p_webDocument,
                      const ExportHTMLOption &p_opt,
                      const QString &p_filePath);

    bool exportToMHTML(VWebView *p_webViewer,
                       const ExportHTMLOption &p_opt,
                       const QString &p_filePath);

    bool htmlsToPDFViaWK(const QList<QString> &p_htmlFiles,
                         const QString &p_filePath,
                         const ExportPDFOption &p_opt,
                         QString *p_errMsg = NULL);

    bool convertFilesViaCustom(const QList<QString> &p_files,
                               const QString &p_filePath,
                               const ExportCustomOption &p_opt,
                               QString *p_errMsg = NULL);

    void prepareWKArguments(const ExportPDFOption &p_opt);

    int startProcess(const QString &p_program, const QStringList &p_args);

    int startProcess(const QString &p_cmd);

    // @p_embedImages: embed <img> as data URI.
    bool outputToHTMLFile(const QString &p_file,
                          const QString &p_headContent,
                          const QString &p_styleContent,
                          const QString &p_bodyContent,
                          bool p_embedCssStyle,
                          bool p_completeHTML,
                          bool p_embedImages);

    // Fix @p_html's resources like url("...") with "file" or "qrc" schema.
    // Copy the resource to @p_folder and fix the url string.
    static bool fixStyleResources(const QString &p_folder,
                                  QString &p_html);

    // Fix @p_html's resources like url("...") with "file" or "qrc" schema.
    // Embed the image data in data URIs.
    static bool embedStyleResources(QString &p_html);

    // Fix @p_html's resources like <img>.
    // Copy the resource to @p_folder and fix the url string.
    static bool fixBodyResources(const QUrl &p_baseUrl,
                                 const QString &p_folder,
                                 QString &p_html);

    // Embed @p_html's resources like <img>.
    static bool embedBodyResources(const QUrl &p_baseUrl, QString &p_html);

    static QString getResourceRelativePath(const QString &p_file);

    QPageLayout m_pageLayout;

    // Will be allocated and free for each conversion.
    VWebView *m_webViewer;

    VDocument *m_webDocument;

    // Base URL of VWebView.
    QUrl m_baseUrl;

    QString m_htmlTemplate;

    // Template to hold the export HTML result.
    QString m_exportHtmlTemplate;

    NoteState m_noteState;

    ExportState m_state;

    // Download state used for MIME HTML.
    QWebEngineDownloadItem::DownloadState m_downloadState;

    // Arguments for wkhtmltopdf.
    QStringList m_wkArgs;

    bool m_askedToStop;
};

inline void VExporter::clearNoteState()
{
    m_noteState = NoteState::NotReady;
}

inline bool VExporter::isNoteStateReady() const
{
    return m_noteState == NoteState::Ready;
}

inline bool VExporter::isNoteStateFailed() const
{
    return m_noteState & NoteState::Failed;
}

inline void VExporter::setAskedToStop(bool p_askedToStop)
{
    m_askedToStop = p_askedToStop;
}
#endif // VEXPORTER_H
