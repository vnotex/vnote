#ifndef VEXPORTER_H
#define VEXPORTER_H

#include <QObject>
#include <QPageLayout>

#include "dialog/vexportdialog.h"

class QWidget;
class VWebView;

class VExporter : public QObject
{
public:
    explicit VExporter(QWidget *p_parent = nullptr);

    void prepareExport(const ExportOption &p_opt);

    bool exportPDF(VFile *p_file,
                   const ExportOption &p_opt,
                   const QString &p_outputFile,
                   QString *p_errMsg = NULL);

private slots:
    void handleLogicsFinished();

    void handleLoadFinished(bool p_ok);

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

    bool exportToPDF(VWebView *p_webViewer,
                     const QString &p_filePath,
                     const QPageLayout &p_layout);

    QPageLayout m_pageLayout;

    // Will be allocated and free for each conversion.
    VWebView *m_webViewer;

    QString m_htmlTemplate;

    NoteState m_noteState;

    ExportState m_state;
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

#endif // VEXPORTER_H
