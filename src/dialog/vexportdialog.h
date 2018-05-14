#ifndef VEXPORTDIALOG_H
#define VEXPORTDIALOG_H

#include <QDialog>
#include <QPageLayout>
#include <QList>
#include <QComboBox>

#include "vconstants.h"

class QLabel;
class VLineEdit;
class QDialogButtonBox;
class QPushButton;
class QGroupBox;
class QPlainTextEdit;
class VNotebook;
class VDirectory;
class VFile;
class VCart;
class VExporter;
class QCheckBox;
class VLineEdit;
class QProgressBar;


enum class ExportSource
{
    CurrentNote = 0,
    CurrentFolder,
    CurrentNotebook,
    Cart
};


enum class ExportFormat
{
    Markdown = 0,
    HTML,
    PDF,
    OnePDF,
    Custom
};


enum class ExportPageNumber
{
    None = 0,
    Left,
    Center,
    Right
};


struct ExportHTMLOption
{
    ExportHTMLOption()
        : m_embedCssStyle(true),
          m_completeHTML(true),
          m_embedImages(true),
          m_mimeHTML(false)
    {
    }

    ExportHTMLOption(bool p_embedCssStyle,
                     bool p_completeHTML,
                     bool p_embedImages,
                     bool p_mimeHTML)
        : m_embedCssStyle(p_embedCssStyle),
          m_completeHTML(p_completeHTML),
          m_embedImages(p_embedImages),
          m_mimeHTML(p_mimeHTML)
    {
    }

    bool m_embedCssStyle;
    bool m_completeHTML;
    bool m_embedImages;
    bool m_mimeHTML;
};


struct ExportPDFOption
{
    ExportPDFOption()
        : m_layout(NULL),
          m_wkhtmltopdf(false),
          m_wkEnableBackground(true),
          m_enableTableOfContents(false),
          m_wkPageNumber(ExportPageNumber::None)
    {
    }

    ExportPDFOption(QPageLayout *p_layout,
                    bool p_wkhtmltopdf,
                    const QString &p_wkPath,
                    bool p_wkEnableBackground,
                    bool p_enableTableOfContents,
                    const QString &p_wkTitle,
                    const QString &p_wkTargetFileName,
                    ExportPageNumber p_wkPageNumber,
                    const QString &p_wkExtraArgs)
        : m_layout(p_layout),
          m_wkhtmltopdf(p_wkhtmltopdf),
          m_wkPath(p_wkPath),
          m_wkEnableBackground(p_wkEnableBackground),
          m_enableTableOfContents(p_enableTableOfContents),
          m_wkTitle(p_wkTitle),
          m_wkTargetFileName(p_wkTargetFileName),
          m_wkPageNumber(p_wkPageNumber),
          m_wkExtraArgs(p_wkExtraArgs)
    {
    }

    QPageLayout *m_layout;
    bool m_wkhtmltopdf;
    QString m_wkPath;
    bool m_wkEnableBackground;
    bool m_enableTableOfContents;;
    QString m_wkTitle;
    QString m_wkTargetFileName;
    ExportPageNumber m_wkPageNumber;
    QString m_wkExtraArgs;
};


struct ExportCustomOption
{
#if defined(Q_OS_WIN)
    #define DEFAULT_SEP ";"
#else
    #define DEFAULT_SEP ":"
#endif

    enum SourceFormat
    {
        Markdown = 0,
        HTML
    };

    ExportCustomOption()
        : m_srcFormat(SourceFormat::Markdown),
          m_allInOne(false),
          m_folderSep(DEFAULT_SEP)
    {
    }

    ExportCustomOption(const QStringList &p_config)
        : m_srcFormat(SourceFormat::Markdown),
          m_allInOne(false),
          m_folderSep(DEFAULT_SEP)
    {
        if (p_config.size() < 3) {
            return;
        }

        if (p_config.at(0).trimmed() != "0") {
            m_srcFormat = SourceFormat::HTML;
        }

        m_outputSuffix = p_config.at(1).trimmed();

        m_cmd = p_config.at(2).trimmed();
    }

    ExportCustomOption(ExportCustomOption::SourceFormat p_srcFormat,
                       const QString &p_outputSuffix,
                       const QString &p_cmd,
                       const QString &p_cssUrl,
                       const QString &p_codeBlockCssUrl,
                       bool p_allInOne,
                       const QString &p_folderSep,
                       const QString &p_targetFileName)
        : m_srcFormat(p_srcFormat),
          m_outputSuffix(p_outputSuffix),
          m_cssUrl(p_cssUrl),
          m_codeBlockCssUrl(p_codeBlockCssUrl),
          m_allInOne(p_allInOne),
          m_folderSep(p_folderSep),
          m_targetFileName(p_targetFileName)
    {
        QStringList cmds = p_cmd.split('\n');
        if (!cmds.isEmpty()) {
            m_cmd = cmds.first();
        }
    }

    QStringList toConfig() const
    {
        QStringList config;
        config << QString::number((int)m_srcFormat);
        config << m_outputSuffix;
        config << m_cmd;

        return config;
    }

    SourceFormat m_srcFormat;
    QString m_outputSuffix;
    QString m_cmd;

    QString m_cssUrl;
    QString m_codeBlockCssUrl;

    bool m_allInOne;

    QString m_folderSep;
    QString m_targetFileName;
};


struct ExportOption
{
    ExportOption()
        : m_source(ExportSource::CurrentNote),
          m_format(ExportFormat::Markdown),
          m_renderer(MarkdownConverterType::MarkdownIt),
          m_processSubfolders(true)
    {
    }

    ExportOption(ExportSource p_source,
                 ExportFormat p_format,
                 MarkdownConverterType p_renderer,
                 const QString &p_renderBg,
                 const QString &p_renderStyle,
                 const QString &p_renderCodeBlockStyle,
                 bool p_processSubfolders,
                 const ExportPDFOption &p_pdfOpt,
                 const ExportHTMLOption &p_htmlOpt,
                 const ExportCustomOption &p_customOpt)
        : m_source(p_source),
          m_format(p_format),
          m_renderer(p_renderer),
          m_renderBg(p_renderBg),
          m_renderStyle(p_renderStyle),
          m_renderCodeBlockStyle(p_renderCodeBlockStyle),
          m_processSubfolders(p_processSubfolders),
          m_pdfOpt(p_pdfOpt),
          m_htmlOpt(p_htmlOpt),
          m_customOpt(p_customOpt)
    {
    }

    ExportSource m_source;
    ExportFormat m_format;
    MarkdownConverterType m_renderer;

    // Background name.
    QString m_renderBg;

    QString m_renderStyle;
    QString m_renderCodeBlockStyle;

    // Whether process subfolders recursively when source is CurrentFolder.
    bool m_processSubfolders;

    ExportPDFOption m_pdfOpt;

    ExportHTMLOption m_htmlOpt;

    ExportCustomOption m_customOpt;
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

    void handleOutputBrowseBtnClicked();

    void handleWkPathBrowseBtnClicked();

    void handleInputChanged();

    void handleLayoutBtnClicked();

    void handleCurrentFormatChanged(int p_index);

    void handleCurrentSrcChanged(int p_index);

private:
    void setupUI();

    QWidget *setupPDFAdvancedSettings();

    QWidget *setupHTMLAdvancedSettings();

    QWidget *setupGeneralAdvancedSettings();

    QWidget *setupCustomAdvancedSettings();

    void initUIFields(MarkdownConverterType p_renderer);

    QString getOutputDirectory() const;

    void appendLogLine(const QString &p_text);

    // Return number of files exported.
    int doExport(VFile *p_file,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL,
                 QList<QString> *p_outputFiles = NULL);

    int doExport(VDirectory *p_directory,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL,
                 QList<QString> *p_outputFiles = NULL);

    int doExport(VNotebook *p_notebook,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL,
                 QList<QString> *p_outputFiles = NULL);

    int doExport(VCart *p_cart,
                 const ExportOption &p_opt,
                 const QString &p_outputFolder,
                 QString *p_errMsg = NULL,
                 QList<QString> *p_outputFiles = NULL);

    int doExportMarkdown(VFile *p_file,
                         const ExportOption &p_opt,
                         const QString &p_outputFolder,
                         QString *p_errMsg = NULL,
                         QList<QString> *p_outputFiles = NULL);

    int doExportPDF(VFile *p_file,
                    const ExportOption &p_opt,
                    const QString &p_outputFolder,
                    QString *p_errMsg = NULL,
                    QList<QString> *p_outputFiles = NULL);

    int doExportHTML(VFile *p_file,
                     const ExportOption &p_opt,
                     const QString &p_outputFolder,
                     QString *p_errMsg = NULL,
                     QList<QString> *p_outputFiles = NULL);

    int doExportPDFAllInOne(const QList<QString> &p_files,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg = NULL);

    int doExportCustomAllInOne(const QList<QString> &p_files,
                               const ExportOption &p_opt,
                               const QString &p_outputFolder,
                               QString *p_errMsg = NULL);

    int doExportCustom(VFile *p_file,
                       const ExportOption &p_opt,
                       const QString &p_outputFolder,
                       QString *p_errMsg = NULL,
                       QList<QString> *p_outputFiles = NULL);

    // Return false if we could not continue.
    bool checkUserAction();

    void updatePageLayoutLabel();

    bool checkWkhtmltopdfExecutable(const QString &p_file);

    ExportSource currentSource() const;

    ExportFormat currentFormat() const;

    int outputAsHTML(const QString &p_outputFolder,
                     QString *p_errMsg = NULL,
                     QList<QString> *p_outputFiles = NULL);

    // Collect files to be handled.
    QList<QString> collectFiles(QString *p_errMsg = NULL);

    QComboBox *m_srcCB;

    QComboBox *m_formatCB;

    QComboBox *m_rendererCB;

    QComboBox *m_renderBgCB;

    QComboBox *m_renderStyleCB;

    QComboBox *m_renderCodeBlockStyleCB;

    VLineEdit *m_outputEdit;

    QGroupBox *m_basicBox;

    QGroupBox *m_settingBox;

    QWidget *m_pdfSettings;

    QWidget *m_htmlSettings;

    QWidget *m_generalSettings;

    QWidget *m_customSettings;

    QPlainTextEdit *m_consoleEdit;

    QDialogButtonBox *m_btnBox;

    QPushButton *m_openBtn;

    QPushButton *m_exportBtn;

    QPushButton *m_copyBtn;

    QLabel *m_layoutLabel;

    QCheckBox *m_wkhtmltopdfCB;

    VLineEdit *m_wkPathEdit;

    QPushButton *m_wkPathBrowseBtn;

    VLineEdit *m_wkTitleEdit;

    VLineEdit *m_wkTargetFileNameEdit;

    QCheckBox *m_wkBackgroundCB;

    QCheckBox *m_tableOfContentsCB;

    QComboBox *m_wkPageNumberCB;

    VLineEdit *m_wkExtraArgsEdit;

    QCheckBox *m_embedStyleCB;

    QCheckBox *m_completeHTMLCB;

    QCheckBox *m_embedImagesCB;

    QCheckBox *m_mimeHTMLCB;

    QCheckBox *m_subfolderCB;

    QComboBox *m_customSrcFormatCB;

    VLineEdit *m_customSuffixEdit;

    QCheckBox *m_customAllInOneCB;

    QPlainTextEdit *m_customCmdEdit;

    VLineEdit *m_customFolderSepEdit;

    VLineEdit *m_customTargetFileNameEdit;

    VNotebook *m_notebook;

    VDirectory *m_directory;

    VFile *m_file;

    VCart *m_cart;

    QProgressBar *m_proBar;

    QPageLayout m_pageLayout;

    // Whether we are exporting files.
    bool m_inExport;

    // Asked to stop exporting by user.
    bool m_askedToStop;

    // Exporter used to export PDF and HTML.
    VExporter *m_exporter;

    // Last exproted file path.
    QString m_exportedFile;

    // Last output folder path.
    static QString s_lastOutputFolder;

    static ExportOption s_opt;
};

inline ExportSource VExportDialog::currentSource() const
{
    return (ExportSource)m_srcCB->currentData().toInt();
}

inline ExportFormat VExportDialog::currentFormat() const
{
    return (ExportFormat)m_formatCB->currentData().toInt();
}
#endif // VEXPORTDIALOG_H
