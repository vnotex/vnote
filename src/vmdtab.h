#ifndef VMDTAB_H
#define VMDTAB_H

#include <QString>
#include <QPointer>
#include "vedittab.h"
#include "vconstants.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"

class VWebView;
class QStackedLayout;
class VEdit;
class VDocument;

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
    void readFile() Q_DECL_OVERRIDE;

    // Save file.
    bool saveFile() Q_DECL_OVERRIDE;

    // Scroll to anchor @p_anchor.
    void scrollToAnchor(const VAnchor& p_anchor) Q_DECL_OVERRIDE;

    void insertImage() Q_DECL_OVERRIDE;

    // Search @p_text in current note.
    void findText(const QString &p_text, uint p_options, bool p_peek,
                  bool p_forward = true) Q_DECL_OVERRIDE;

    // Replace @p_text with @p_replaceText in current note.
    void replaceText(const QString &p_text, uint p_options,
                     const QString &p_replaceText, bool p_findNext) Q_DECL_OVERRIDE;

    void replaceTextAll(const QString &p_text, uint p_options,
                        const QString &p_replaceText) Q_DECL_OVERRIDE;

    QString getSelectedText() const Q_DECL_OVERRIDE;

    void clearSearchedWordHighlight() Q_DECL_OVERRIDE;

    VWebView *getWebViewer() const;

    MarkdownConverterType getMarkdownConverterType() const;

    void requestUpdateVimStatus() Q_DECL_OVERRIDE;

    // Insert decoration markers or decorate selected text.
    void decorateText(TextDecoration p_decoration) Q_DECL_OVERRIDE;

public slots:
    // Enter edit mode.
    void editFile() Q_DECL_OVERRIDE;

private slots:
    // Handle text changed in m_editor.
    void handleTextChanged();

    // Update m_toc according to @p_tocHtml for read mode.
    void updateTocFromHtml(const QString &p_tocHtml);

    // Update m_toc accroding to @p_headers for edit mode.
    void updateTocFromHeaders(const QVector<VHeader> &p_headers);

    // Web viewer requests to update current header.
    void updateCurHeader(const QString &p_anchor);

    // Editor requests to update current header.
    void updateCurHeader(VAnchor p_anchor);

    // Handle key press event in Web view.
    void handleWebKeyPressed(int p_key, bool p_ctrl, bool p_shift);

    // m_editor requests to save changes and enter read mode.
    void saveAndRead();

    // m_editor requests to discard changes and enter read mode.
    void discardAndRead();

private:
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
    // @p_outlineIndex is the index in m_toc.headers.
    void scrollWebViewToHeader(int p_outlineIndex);

    // Search text in Web view.
    void findTextInWebView(const QString &p_text, uint p_options, bool p_peek,
                           bool p_forward);

    // Called to zoom in/out content.
    void zoom(bool p_zoomIn, qreal p_step = 0.25) Q_DECL_OVERRIDE;

    // Zoom Web View.
    void zoomWebPage(bool p_zoomIn, qreal p_step = 0.25);

    // Focus the proper child widget.
    void focusChild() Q_DECL_OVERRIDE;

    // Create a filled VEditTabInfo.
    VEditTabInfo createEditTabInfo() Q_DECL_OVERRIDE;

    // Get the markdown editor. If not init yet, init and return it.
    VEdit *getEditor();

    VEdit *m_editor;
    VWebView *m_webViewer;
    VDocument *m_document;
    MarkdownConverterType m_mdConType;

    QStackedLayout *m_stacks;
};

inline VEdit *VMdTab::getEditor()
{
    if (m_editor) {
        return m_editor;
    } else if (m_file->isModifiable()) {
        setupMarkdownEditor();
    }

    return m_editor;
}

#endif // VMDTAB_H
