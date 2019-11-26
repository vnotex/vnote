#ifndef VMDEDITOR_H
#define VMDEDITOR_H

#include <QVector>
#include <QString>
#include <QClipboard>
#include <QImage>
#include <QUrl>
#include <QTemporaryFile>
#include <QSharedPointer>

#include "vtextedit.h"
#include "veditor.h"
#include "vconfigmanager.h"
#include "vtableofcontent.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

class PegMarkdownHighlighter;
class VCodeBlockHighlightHelper;
class VDocument;
class VPreviewManager;
class VCopyTextAsHtmlDialog;
class VEditTab;
class VTableHelper;

class VMdEditor : public VTextEdit, public VEditor
{
    Q_OBJECT
public:
    VMdEditor(VFile *p_file,
              VDocument *p_doc,
              MarkdownConverterType p_type,
              const QSharedPointer<VTextEditCompleter> &p_completer,
              QWidget *p_parent = nullptr);

    void beginEdit() Q_DECL_OVERRIDE;

    void endEdit() Q_DECL_OVERRIDE;

    void saveFile() Q_DECL_OVERRIDE;

    void reloadFile() Q_DECL_OVERRIDE;

    bool scrollToBlock(int p_blockNumber) Q_DECL_OVERRIDE;

    void makeBlockVisible(const QTextBlock &p_block) Q_DECL_OVERRIDE;

    QVariant inputMethodQuery(Qt::InputMethodQuery p_query) const Q_DECL_OVERRIDE;

    bool isBlockVisible(const QTextBlock &p_block) Q_DECL_OVERRIDE;

    // An image has been inserted. The image is relative.
    // @p_path is the absolute path of the inserted image.
    // @p_url is the URL text within ().
    void imageInserted(const QString &p_path, const QString &p_url);

    // Scroll to header @p_blockNumber.
    // Return true if @p_blockNumber is valid to scroll to.
    bool scrollToHeader(int p_blockNumber);

    void scrollBlockInPage(int p_blockNum, int p_dest, int p_margin = 0) Q_DECL_OVERRIDE;

    void updateConfig() Q_DECL_OVERRIDE;

    QString getContent() const Q_DECL_OVERRIDE;

    void setContent(const QString &p_content, bool p_modified = false) Q_DECL_OVERRIDE;

    void refreshPreview();

    // Update m_initImages and m_insertedImages to handle the change of the note path.
    void updateInitAndInsertedImages(bool p_fileChanged, UpdateAction p_act);

    VWordCountInfo fetchWordCountInfo() const Q_DECL_OVERRIDE;

    void setEditTab(VEditTab *p_editTab);

    PegMarkdownHighlighter *getMarkdownHighlighter() const;

    VPreviewManager *getPreviewManager() const;

    void updateHeaderSequenceByConfigChange();

    void updateFontAndPalette() Q_DECL_OVERRIDE;

    void insertTable() Q_DECL_OVERRIDE;

public slots:
    bool jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat) Q_DECL_OVERRIDE;

    void textToHtmlFinished(int p_id, int p_timeStamp, const QUrl &p_baseUrl, const QString &p_html);

    void htmlToTextFinished(int p_id, int p_timeStamp, const QString &p_html);

    void parseAndPaste();

// Wrapper functions for QPlainTextEdit/QTextEdit.
public:
    void setExtraSelectionsW(const QList<QTextEdit::ExtraSelection> &p_selections) Q_DECL_OVERRIDE
    {
        setExtraSelections(p_selections);
    }

    QTextDocument *documentW() const Q_DECL_OVERRIDE
    {
        return document();
    }

    int tabStopWidthW() const Q_DECL_OVERRIDE
    {
        return tabStopWidth();
    }

    void setTabStopWidthW(int p_width) Q_DECL_OVERRIDE
    {
        setTabStopWidth(p_width);
    }

    QTextCursor textCursorW() const Q_DECL_OVERRIDE
    {
        return textCursor();
    }

    void moveCursorW(QTextCursor::MoveOperation p_operation,
                     QTextCursor::MoveMode p_mode) Q_DECL_OVERRIDE
    {
        moveCursor(p_operation, p_mode);
    }

    QScrollBar *verticalScrollBarW() const Q_DECL_OVERRIDE
    {
        return verticalScrollBar();
    }

    QScrollBar *horizontalScrollBarW() const Q_DECL_OVERRIDE
    {
        return horizontalScrollBar();
    }

    void setTextCursorW(const QTextCursor &p_cursor) Q_DECL_OVERRIDE
    {
        setTextCursor(p_cursor);
    }

    bool findW(const QString &p_exp,
               QTextDocument::FindFlags p_options = QTextDocument::FindFlags()) Q_DECL_OVERRIDE
    {
        return find(p_exp, p_options);
    }

    bool findW(const QRegExp &p_exp,
               QTextDocument::FindFlags p_options = QTextDocument::FindFlags()) Q_DECL_OVERRIDE
    {
        return find(p_exp, p_options);
    }

    bool isReadOnlyW() const Q_DECL_OVERRIDE
    {
        return isReadOnly();
    }

    void setReadOnlyW(bool p_ro) Q_DECL_OVERRIDE
    {
        setReadOnly(p_ro);
    }

    QWidget *viewportW() const Q_DECL_OVERRIDE
    {
        return viewport();
    }

    void insertPlainTextW(const QString &p_text) Q_DECL_OVERRIDE
    {
        insertPlainText(p_text);
    }

    void undoW() Q_DECL_OVERRIDE
    {
        undo();
    }

    void redoW() Q_DECL_OVERRIDE
    {
        redo();
    }

    // Whether display cursor as block.
    void setCursorBlockModeW(CursorBlock p_mode) Q_DECL_OVERRIDE
    {
        setCursorBlockMode(p_mode);
    }

    void zoomInW(int p_range = 1) Q_DECL_OVERRIDE
    {
        zoomPage(true, p_range);
    }

    void zoomOutW(int p_range = 1) Q_DECL_OVERRIDE
    {
        zoomPage(false, p_range);
    }

    void ensureCursorVisibleW() Q_DECL_OVERRIDE
    {
        ensureCursorVisible();
    }

    QRect cursorRectW() Q_DECL_OVERRIDE
    {
        return cursorRect();
    }

    QRect cursorRectW(const QTextCursor &p_cursor) Q_DECL_OVERRIDE
    {
        return cursorRect(p_cursor);
    }

signals:
    // Signal when headers change.
    void headersChanged(const QVector<VTableOfContentItem> &p_headers);

    // Signal when current header change.
    void currentHeaderChanged(int p_blockNumber);

    // Signal when the status of VMdEdit changed.
    // Will be emitted by VImagePreviewer for now.
    void statusChanged();

    // Request to convert @p_text to Html.
    void requestTextToHtml(const QString &p_text, int p_id, int p_timeStamp);

    // Request to convert @p_html to Markdown text.
    void requestHtmlToText(const QString &p_html, int p_id, int p_timeStamp);

    void requestUploadImageToGithub();

    void requestUploadImageToGitee();

    void requestUploadImageToWechat();

    void requestUploadImageToTencent();

protected:
    void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

    // Used to implement dragging mouse with Ctrl and left button pressed to scroll.
    void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    void mouseReleaseEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    void mouseMoveEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    void mouseDoubleClickEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    bool canInsertFromMimeData(const QMimeData *p_source) const Q_DECL_OVERRIDE;

    void insertFromMimeData(const QMimeData *p_source) Q_DECL_OVERRIDE;

    void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

    int lineNumberAreaWidth() const Q_DECL_OVERRIDE;

private slots:
    // Update m_headers according to elements.
    void updateHeaders(const QVector<VElementRegion> &p_headerRegions);

    // Update current header according to cursor position.
    // When there is no header in current cursor, will signal an invalid header.
    void updateCurrentHeader();

    // Copy selected text as HTML.
    void handleCopyAsAction(QAction *p_act);

    void handleLinkToAttachmentAction(QAction *p_act);

private:
    void updateHeadersHelper(const QVector<VElementRegion> &p_headerRegions, bool p_configChanged);

    // Update the config of VTextEdit according to global configurations.
    void updateTextEditConfig();

    // Get the initial images from file before edit.
    void initInitImages();

    // Clear two kind of images according to initial images and current images:
    // 1. Newly inserted images which are deleted later;
    // 2. Initial images which are deleted;
    void clearUnusedImages();

    // Index in m_headers of current header which contains the cursor.
    int indexOfCurrentHeader() const;

    // Zoom in/out.
    // We need to maintain the styles font size.
    void zoomPage(bool p_zoomIn, int p_range = 1);

    QAction *initCopyAsMenu(QAction *p_after, QMenu *p_menu);

    QAction *initPasteAsBlockQuoteMenu(QAction *p_after, QMenu *p_menu);

    QAction *initPasteAfterParseMenu(QAction *p_after, QMenu *p_menu);

    void initLinkAndPreviewMenu(QAction *p_before, QMenu *p_menu, const QPoint &p_pos);

    bool initInPlacePreviewMenu(QAction *p_before,
                                QMenu *p_menu,
                                const QTextBlock &p_block,
                                int p_pos);

    bool initExportAndCopyMenu(QAction *p_before,
                               QMenu *p_menu,
                               const QTextBlock &p_block,
                               int p_pos);

    void initAttachmentMenu(QMenu *p_menu);

    void initImageHostingMenu(QMenu *p_menu);

    void insertImageLink(const QString &p_text, const QString &p_url);

    void setFontPointSizeByStyleSheet(int p_ptSize);

    void setFontAndPaletteByStyleSheet(const QFont &p_font, const QPalette &p_palette);

    void exportGraphAndCopy(const QString &p_lang,
                            const QString &p_text,
                            const QString &p_format);

    bool processHtmlFromMimeData(const QMimeData *p_source);

    bool processImageFromMimeData(const QMimeData *p_source);

    bool processUrlFromMimeData(const QMimeData *p_source);

    void replaceTextWithLocalImages(QString &p_text);

    PegMarkdownHighlighter *m_pegHighlighter;

    VCodeBlockHighlightHelper *m_cbHighlighter;

    VPreviewManager *m_previewMgr;

    VTableHelper *m_tableHelper;

    // Image links inserted while editing.
    QVector<ImageLink> m_insertedImages;

    // Image links right at the beginning of the edit.
    QVector<ImageLink> m_initImages;

    // Mainly used for title jump.
    QVector<VTableOfContentItem> m_headers;

    bool m_freshEdit;

    VCopyTextAsHtmlDialog *m_textToHtmlDialog;

    int m_zoomDelta;

    VEditTab *m_editTab;

    int m_copyTimeStamp;

    // Temp file used for ExportAndCopy.
    QSharedPointer<QTemporaryFile> m_exportTempFile;
};

inline PegMarkdownHighlighter *VMdEditor::getMarkdownHighlighter() const
{
    return m_pegHighlighter;
}

inline VPreviewManager *VMdEditor::getPreviewManager() const
{
    return m_previewMgr;
}
#endif // VMDEDITOR_H
