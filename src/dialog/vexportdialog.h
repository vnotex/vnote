#ifndef VEXPORTDIALOG_H
#define VEXPORTDIALOG_H

#include <QDialog>
#include <QPageLayout>

#include "vconstants.h"

class QLabel;
class VLineEdit;
class QDialogButtonBox;
class QComboBox;
class QPushButton;
class QGroupBox;
class QPlainTextEdit;
class VNotebook;
class VDirectory;
class VFile;
class VCart;
class VExporter;
class QCheckBox;


enum class ExportSource
{
    CurrentNote = 0,
    CurrentDirectory,
    CurrentNotebook,
    Cart
};


enum class ExportFormat
{
    Markdown = 0,
    HTML,
    PDF
};


struct ExportHTMLOption
{
    ExportHTMLOption(bool p_embedCssStyle,
                     bool p_completeHTML,
                     bool p_mimeHTML)
        : m_embedCssStyle(p_embedCssStyle),
          m_completeHTML(p_completeHTML),
          m_mimeHTML(p_mimeHTML)
    {
    }

    bool m_embedCssStyle;
    bool m_completeHTML;
    bool m_mimeHTML;
};


struct ExportOption
{
    ExportOption(ExportSource p_source,
                 ExportFormat p_format,
                 MarkdownConverterType p_renderer,
                 const QString &p_renderBg,
                 const QString &p_renderStyle,
                 const QString &p_renderCodeBlockStyle,
                 QPageLayout *p_layout,
                 const ExportHTMLOption &p_htmlOpt)
        : m_source(p_source),
          m_format(p_format),
          m_renderer(p_renderer),
          m_renderBg(p_renderBg),
          m_renderStyle(p_renderStyle),
          m_renderCodeBlockStyle(p_renderCodeBlockStyle),
          m_layout(p_layout),
          m_htmlOpt(p_htmlOpt)
    {
    }

    ExportSource m_source;
    ExportFormat m_format;
    MarkdownConverterType m_renderer;

    // Background name.
    QString m_renderBg;

    QString m_renderStyle;
    QString m_renderCodeBlockStyle;
    QPageLayout *m_layout;

    ExportHTMLOption m_htmlOpt;
};


class VExportDialog : public QDialog
{
    Q_OBJECT
public:
    VExportDialog(VNotebook *p_notebook,
                  VDirectory *p_directory,
                  VFile *p_file,
                  VCart *p_cart,
                  MarkdownConverterType p_renderer,
                  QWidget *p_parent = nullptr);

private slots:
    void startExport();

    void handleBrowseBtnClicked();

    void handleInputChanged();

    void handleLayoutBtnClicked();

    void handleCurrentFormatChanged(int p_index);

private:
    void setupUI();

    QWidget *setupPDFAdvancedSettings();

    QWidget *setupHTMLAdvancedSettings();

    void initUIFields(MarkdownConverterType p_renderer);

    QString getOutputDirectory() const;

    void appendLogLine(const QString &p_text);

    // Return number of files exported.
    int doExport(VFile *p_file,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL);

    int doExport(VDirectory *p_directory,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL);

    int doExport(VNotebook *p_notebook,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL);

    int doExport(VCart *p_cart,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL);

    int doExportMarkdown(VFile *p_file,
                         const ExportOption &p_opt,
                         const QString &p_outputFolder,
                         QString *p_errMsg = NULL);

    int doExportPDF(VFile *p_file,
                    const ExportOption &p_opt,
                    const QString &p_outputFolder,
                    QString *p_errMsg = NULL);

    int doExportHTML(VFile *p_file,
                     const ExportOption &p_opt,
                     const QString &p_outputFolder,
                     QString *p_errMsg = NULL);

    // Return false if we could not continue.
    bool checkUserAction();

    void updatePageLayoutLabel();

    QComboBox *m_srcCB;

    QComboBox *m_formatCB;

    QComboBox *m_rendererCB;

    QComboBox *m_renderBgCB;

    QComboBox *m_renderStyleCB;

    QComboBox *m_renderCodeBlockStyleCB;

    VLineEdit *m_outputEdit;

    QPushButton *m_browseBtn;

    QGroupBox *m_basicBox;

    QGroupBox *m_settingBox;

    QWidget *m_pdfSettings;

    QWidget *m_htmlSettings;

    QPlainTextEdit *m_consoleEdit;

    QDialogButtonBox *m_btnBox;

    QPushButton *m_openBtn;

    QPushButton *m_exportBtn;

    QLabel *m_layoutLabel;

    QCheckBox *m_embedStyleCB;

    QCheckBox *m_completeHTMLCB;;

    QCheckBox *m_mimeHTMLCB;

    VNotebook *m_notebook;

    VDirectory *m_directory;

    VFile *m_file;

    VCart *m_cart;

    QPageLayout m_pageLayout;

    // Whether we are exporting files.
    bool m_inExport;

    // Asked to stop exporting by user.
    bool m_askedToStop;

    // Exporter used to export PDF and HTML.
    VExporter *m_exporter;

    // Last output folder path.
    static QString s_lastOutputFolder;

    // Last export format.
    static ExportFormat s_lastExportFormat;
};

#endif // VEXPORTDIALOG_H
