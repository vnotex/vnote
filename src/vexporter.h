#ifndef VEXPORTER_H
#define VEXPORTER_H

#include <QDialog>
#include <QPageLayout>
#include <QString>
#include "vconfigmanager.h"

class VWebView;
class VFile;
class QLineEdit;
class QLabel;
class QDialogButtonBox;
class QPushButton;
class QProgressBar;

enum class ExportType
{
    PDF = 0,
    HTML
};

class VExporter : public QDialog
{
    Q_OBJECT
public:
    explicit VExporter(MarkdownConverterType p_mdType = MarkdownIt, QWidget *p_parent = 0);

    void exportNote(VFile *p_file, ExportType p_type);

private slots:
    void handleBrowseBtnClicked();
    void handleLayoutBtnClicked();
    void startExport();
    void cancelExport();
    void handleLogicsFinished();
    void handleLoadFinished(bool p_ok);
    void openTargetPath() const;

private:
    enum class ExportSource
    {
        Note = 0,
        Directory,
        Notebook,
        Invalid
    };

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

    void setupUI();

    void initMarkdownTemplate();

    void updatePageLayoutLabel();

    void setFilePath(const QString &p_path);

    QString getFilePath() const;

    void initWebViewer(VFile *p_file);

    void clearWebViewer();

    void enableUserInput(bool p_enabled);

    bool exportToPDF(VWebView *p_webViewer, const QString &p_filePath, const QPageLayout &p_layout);

    void clearNoteState();
    bool isNoteStateReady() const;
    bool isNoteStateFailed() const;

    // Will be allocated and free for each conversion.
    VWebView *m_webViewer;

    MarkdownConverterType m_mdType;
    QString m_htmlTemplate;
    VFile *m_file;
    ExportType m_type;
    ExportSource m_source;
    NoteState m_noteState;

    ExportState m_state;

    QLabel *m_infoLabel;
    QLineEdit *m_pathEdit;
    QPushButton *m_browseBtn;
    QLabel *m_layoutLabel;
    QPushButton *m_layoutBtn;
    QDialogButtonBox *m_btnBox;
    QPushButton *m_openBtn;

    // Progress label and bar.
    QLabel *m_proLabel;
    QProgressBar *m_proBar;

    QPageLayout m_pageLayout;

    // Whether a PDF has been exported.
    bool m_exported;

    // The default directory.
    static QString s_defaultPathDir;
};

#endif // VEXPORTER_H
