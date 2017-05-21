#ifndef VEXPORTER_H
#define VEXPORTER_H

#include <QDialog>
#include <QPageLayout>
#include <QString>
#include "vconfigmanager.h"
#include "vdocument.h"

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
    Busy
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

private:
    void setupUI();

    // Init m_webViewer, m_document, and m_htmlTemplate.
    void setupMarkdownViewer();

    void updatePageLayoutLabel();

    void setFilePath(const QString &p_path);

    QString getFilePath() const;

    void updateWebViewer(VFile *p_file);

    void readyToExport();

    void enableUserInput(bool p_enabled);

    void exportToPDF(VWebView *p_webViewer, const QString &p_filePath, const QPageLayout &p_layout);

    VWebView *m_webViewer;
    VDocument m_document;
    MarkdownConverterType m_mdType;
    QString m_htmlTemplate;
    VFile *m_file;
    ExportType m_type;
    ExportSource m_source;
    bool m_webReady;

    ExportState m_state;

    QLabel *m_infoLabel;
    QLineEdit *m_pathEdit;
    QPushButton *m_browseBtn;
    QLabel *m_layoutLabel;
    QPushButton *m_layoutBtn;
    QDialogButtonBox *m_btnBox;

    // Progress label and bar.
    QLabel *m_proLabel;
    QProgressBar *m_proBar;

    QPageLayout m_pageLayout;

    // The default directory.
    static QString s_defaultPathDir;
};

#endif // VEXPORTER_H
