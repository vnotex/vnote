#ifndef VMDTAB_H
#define VMDTAB_H

#include <QString>
#include <QPointer>
#include <QSharedPointer>
#include "vedittab.h"
#include "vconstants.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"
#include "vimagehosting.h"

class VWebView;
class VDocument;
class VMdEditor;
class VInsertSelector;
class QTimer;
class QWebEngineDownloadItem;
class QSplitter;
class VLivePreviewHelper;
class VMathJaxInplacePreviewHelper;

class VMdTab : public VEditTab
{
    Q_OBJECT

public:
    VMdTab(VFile *p_file, VEditArea *p_editArea, OpenFileMode p_mode, QWidget *p_parent = 0);

    // Close current tab.
    // @p_forced: if true, discard the changes.
    bool closeFile(bool p_forced) Q_DECL_OVERRIDE;

    // Enter read mode.
    // Will prompt user to save the changes.
    void readFile(bool p_discard = false) Q_DECL_OVERRIDE;

    // Save file.
    bool saveFile() Q_DECL_OVERRIDE;

    bool isModified() const Q_DECL_OVERRIDE;

    // Scroll to @p_header.
    void scrollToHeader(const VHeaderPointer &p_header) Q_DECL_OVERRIDE;

    void insertImage() Q_DECL_OVERRIDE;

    void insertTable() Q_DECL_OVERRIDE;

    void insertLink() Q_DECL_OVERRIDE;

    // Search @p_text in current note.
    void findText(const QString &p_text, uint p_options, bool p_peek,
                  bool p_forward = true) Q_DECL_OVERRIDE;

    void findText(const VSearchToken &p_token,
                  bool p_forward = true,
                  bool p_fromStart = false) Q_DECL_OVERRIDE;

    // Replace @p_text with @p_replaceText in current note.
    void replaceText(const QString &p_text, uint p_options,
                     const QString &p_replaceText, bool p_findNext) Q_DECL_OVERRIDE;

    void replaceTextAll(const QString &p_text, uint p_options,
                        const QString &p_replaceText) Q_DECL_OVERRIDE;

    void nextMatch(const QString &p_text, uint p_options, bool p_forward) Q_DECL_OVERRIDE;

    QString getSelectedText() const Q_DECL_OVERRIDE;

    void clearSearchedWordHighlight() Q_DECL_OVERRIDE;

    VWebView *getWebViewer() const;
    
    // Get the markdown editor. If not init yet, init and return it.
    VMdEditor *getEditor();

    VMdEditor *getEditor() const;

    MarkdownConverterType getMarkdownConverterType() const;

    void requestUpdateVimStatus() Q_DECL_OVERRIDE;

    // Insert decoration markers or decorate selected text.
    void decorateText(TextDecoration p_decoration, int p_level = -1) Q_DECL_OVERRIDE;

    // Create a filled VEditTabInfo.
    VEditTabInfo fetchTabInfo(VEditTabInfo::InfoType p_type = VEditTabInfo::InfoType::All) const Q_DECL_OVERRIDE;

    // Enable or disable heading sequence.
    void enableHeadingSequence(bool p_enabled);

    bool isHeadingSequenceEnabled() const;

    // Evaluate magic words.
    void evaluateMagicWords() Q_DECL_OVERRIDE;

    void applySnippet(const VSnippet *p_snippet) Q_DECL_OVERRIDE;

    void applySnippet() Q_DECL_OVERRIDE;

    void reload() Q_DECL_OVERRIDE;

    void handleFileOrDirectoryChange(bool p_isFile, UpdateAction p_act) Q_DECL_OVERRIDE;

    // Fetch tab stat info.
    VWordCountInfo fetchWordCountInfo(bool p_editMode) const Q_DECL_OVERRIDE;

    // Toggle live preview in edit mode.
    bool toggleLivePreview();

    bool expandRestorePreviewArea();

public slots:
    // Enter edit mode.
    void editFile() Q_DECL_OVERRIDE;

    void handleVimCmdCommandCancelled() Q_DECL_OVERRIDE;

    void handleVimCmdCommandFinished(VVim::CommandLineType p_type, const QString &p_cmd) Q_DECL_OVERRIDE;

    void handleVimCmdCommandChanged(VVim::CommandLineType p_type, const QString &p_cmd) Q_DECL_OVERRIDE;

    QString handleVimCmdRequestNextCommand(VVim::CommandLineType p_type, const QString &p_cmd) Q_DECL_OVERRIDE;

    QString handleVimCmdRequestPreviousCommand(VVim::CommandLineType p_type, const QString &p_cmd) Q_DECL_OVERRIDE;

    QString handleVimCmdRequestRegister(int p_key, int p_modifiers) Q_DECL_OVERRIDE;

protected:
    void writeBackupFile() Q_DECL_OVERRIDE;

private slots:
    // Update m_outline according to @p_tocHtml for read mode.
    void updateOutlineFromHtml(const QString &p_tocHtml);

    // Update m_outline accroding to @p_headers for edit mode.
    void updateOutlineFromHeaders(const QVector<VTableOfContentItem> &p_headers);

    // Web viewer requests to update current header.
    // @p_anchor is the anchor of the header, like "toc_1".
    void updateCurrentHeader(const QString &p_anchor);

    // Editor requests to update current header.
    void updateCurrentHeader(int p_blockNumber);

    // Handle key press event in Web view.
    void handleWebKeyPressed(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

    // m_editor requests to save changes and enter read mode.
    void saveAndRead();

    // m_editor requests to discard changes and enter read mode.
    void discardAndRead();

    // Restore from m_infoToRestore.
    void restoreFromTabInfo();

    // Handle download request from web page.
    void handleDownloadRequested(QWebEngineDownloadItem *p_item);

    // Handle save page request.
    void handleSavePageRequested();

    // Selection changed in web.
    void handleWebSelectionChanged();

    // Process the image upload request to GitHub.
    void handleUploadImageToGithubRequested();

    // Process the image upload request to Gitee.
    void handleUploadImageToGiteeRequested();

    // Process image upload request to wechat.
    void handleUploadImageToWechatRequested();

    // Process image upload request to tencent.
    void handleUploadImageToTencentRequested();

private:
    enum TabReady { None = 0, ReadMode = 0x1, EditMode = 0x2 };

    enum Mode { InvalidMode = 0, Read, Edit, EditPreview };

    struct WebViewState
    {
        qreal m_zoomFactor;
    };

    // Setup UI.
    void setupUI();

    // Show the file content in read mode.
    void showFileReadMode();

    // Show the file content in edit mode.
    void showFileEditMode();

    // Setup Markdown viewer.
    void setupMarkdownViewer();

    // Setup Markdown editor.
    void setupMarkdownEditor();

    // Use VMarkdownConverter (hoedown) to generate the Web view.
    void viewWebByConverter();

    // Scroll Web view to given header.
    // Return true if scroll was made.
    bool scrollWebViewToHeader(const VHeaderPointer &p_header);

    // @p_force: when true, will scroll even current mouse is under the specified header.
    bool scrollEditorToHeader(const VHeaderPointer &p_header, bool p_force = true);

    // Scroll web/editor to given header.
    // Return true if scroll was made.
    bool scrollToHeaderInternal(const VHeaderPointer &p_header);

    // Search text in Web view.
    void findTextInWebView(const QString &p_text, uint p_options, bool p_peek,
                           bool p_forward);

    // Called to zoom in/out content.
    void zoom(bool p_zoomIn, qreal p_step = 0.25) Q_DECL_OVERRIDE;

    // Zoom Web View.
    void zoomWebPage(bool p_zoomIn, qreal p_step = 0.25);

    // Focus the proper child widget.
    void focusChild() Q_DECL_OVERRIDE;

    // Restore from @p_fino.
    // Return true if succeed.
    bool restoreFromTabInfo(const VEditTabInfo &p_info) Q_DECL_OVERRIDE;

    // Prepare insert selector with snippets.
    VInsertSelector *prepareSnippetSelector(QWidget *p_parent = nullptr);

    // Called once read or edit mode is ready.
    void tabIsReady(TabReady p_mode);

    // Check if there exists backup file from previous session.
    // Return true if we could continue.
    bool checkPreviousBackupFile();

    // updateStatus() with only cursor position information.
    void updateCursorStatus();

    void textToHtmlViaWebView(const QString &p_text, int p_id, int p_timeStamp);

    void htmlToTextViaWebView(const QString &p_html, int p_id, int p_timeStamp);

    bool executeVimCommandInWebView(const QString &p_cmd);

    // Update web view by current content.
    void updateWebView();

    void setCurrentMode(Mode p_mode);

    bool previewExpanded() const;

    VMdEditor *m_editor;
    VWebView *m_webViewer;
    VDocument *m_document;
    MarkdownConverterType m_mdConType;

    // Whether heading sequence is enabled.
    bool m_enableHeadingSequence;

    QSplitter *m_splitter;

    // Timer to write backup file when content has been changed.
    QTimer *m_backupTimer;

    QTimer *m_livePreviewTimer;

    bool m_backupFileChecked;

    // Used to scroll to the header of edit mode in read mode.
    VHeaderPointer m_headerFromEditMode;

    VVim::SearchItem m_lastSearchItem;

    Mode m_mode;

    QSharedPointer<WebViewState> m_readWebViewState;
    QSharedPointer<WebViewState> m_previewWebViewState;

    VLivePreviewHelper *m_livePreviewHelper;
    VMathJaxInplacePreviewHelper *m_mathjaxPreviewHelper;

    int m_documentID;

    VGithubImageHosting *vGithubImageHosting;
    VGiteeImageHosting *vGiteeImageHosting;
    VWechatImageHosting *vWechatImageHosting;
    VTencentImageHosting * vTencentImageHosting;
};

inline VMdEditor *VMdTab::getEditor()
{
    if (m_editor) {
        return m_editor;
    } else {
        setupMarkdownEditor();
        return m_editor;
    }
}

inline VMdEditor *VMdTab::getEditor() const
{
    return m_editor;
}

#endif // VMDTAB_H
