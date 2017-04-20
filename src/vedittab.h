#ifndef VEDITTAB_H
#define VEDITTAB_H

#include <QStackedWidget>
#include <QString>
#include <QPointer>
#include "vconstants.h"
#include "vdocument.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"
#include "vedit.h"
#include "vtoc.h"
#include "vfile.h"

class VWebView;
class VNote;
class QXmlStreamReader;
class VEditArea;

class VEditTab : public QStackedWidget
{
    Q_OBJECT
public:
    VEditTab(VFile *p_file, OpenFileMode p_mode, QWidget *p_parent = 0);
    ~VEditTab();
    void init(VEditArea *p_editArea);
    bool closeFile(bool p_forced);
    // Enter read mode
    void readFile();
    // Save file
    bool saveFile();

    inline bool getIsEditMode() const;
    inline bool isModified() const;
    void focusTab();
    void requestUpdateOutline();
    void requestUpdateCurHeader();
    void scrollToAnchor(const VAnchor& anchor);
    inline VFile *getFile();
    void insertImage();
    // Search @p_text in current note.
    void findText(const QString &p_text, uint p_options, bool p_peek,
                  bool p_forward = true);
    // Replace @p_text with @p_replaceText in current note.
    void replaceText(const QString &p_text, uint p_options,
                     const QString &p_replaceText, bool p_findNext);
    void replaceTextAll(const QString &p_text, uint p_options,
                        const QString &p_replaceText);
    QString getSelectedText() const;
    void clearSearchedWordHighlight();

public slots:
    // Enter edit mode
    void editFile();

protected:
    void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

signals:
    void getFocused();
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);
    void statusChanged();

private slots:
    void handleFocusChanged(QWidget *old, QWidget *now);
    void updateTocFromHtml(const QString &tocHtml);
    void updateCurHeader(const QString &anchor);
    void updateCurHeader(int p_lineNumber, int p_outlineIndex);
    void updateTocFromHeaders(const QVector<VHeader> &headers);
    void handleTextChanged();
    void noticeStatusChanged();
    void handleWebKeyPressed(int p_key, bool p_ctrl, bool p_shift);

private:
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();
    void setupMarkdownPreview();
    void previewByConverter();
    void processHoedownToc(QString &p_toc);
    inline bool isChild(QObject *obj);
    void parseTocUl(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);
    void parseTocLi(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);
    void scrollPreviewToHeader(int p_outlineIndex);
    void findTextInWebView(const QString &p_text, uint p_options, bool p_peek,
                           bool p_forward);
    // Check if @tableOfContent is outdated (such as renaming the file).
    // Return true if we need to update toc.
    bool checkToc();
    void zoomWebPage(bool p_zoomIn, qreal p_step = 0.25);

    QPointer<VFile> m_file;
    bool isEditMode;
    VEdit *m_textEditor;
    VWebView *webPreviewer;
    VDocument document;
    MarkdownConverterType mdConverterType;
    VToc tableOfContent;
    VAnchor curHeader;
    bool m_fileModified;
    VEditArea *m_editArea;
};

inline bool VEditTab::getIsEditMode() const
{
    return isEditMode;
}

inline bool VEditTab::isModified() const
{
    return m_textEditor->isModified();
}

inline bool VEditTab::isChild(QObject *obj)
{
    while (obj) {
        if (obj == this) {
            return true;
        }
        obj = obj->parent();
    }
    return false;
}

inline VFile *VEditTab::getFile()
{
    return m_file;
}

#endif // VEDITTAB_H
