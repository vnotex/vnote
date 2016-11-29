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

class QTextBrowser;
class QWebEngineView;
class VNote;
class QXmlStreamReader;

class VEditTab : public QStackedWidget
{
    Q_OBJECT
public:
    VEditTab(VFile *p_file, OpenFileMode p_mode, QWidget *p_parent = 0);
    ~VEditTab();
    bool closeFile(bool p_forced);
    // Enter edit mode
    void editFile();
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
signals:
    void getFocused();
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);
    void statusChanged();

private slots:
    void handleFocusChanged(QWidget *old, QWidget *now);
    void updateTocFromHtml(const QString &tocHtml);
    void updateCurHeader(const QString &anchor);
    void updateCurHeader(int lineNumber);
    void updateTocFromHeaders(const QVector<VHeader> &headers);

private:
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();
    void setupMarkdownPreview();
    void previewByConverter();
    inline bool isChild(QObject *obj);
    void parseTocUl(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);
    void parseTocLi(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);

    QPointer<VFile> m_file;
    bool isEditMode;
    QTextBrowser *textBrowser;
    VEdit *textEditor;
    QWebEngineView *webPreviewer;
    VDocument document;
    MarkdownConverterType mdConverterType;
    VToc tableOfContent;
    VAnchor curHeader;
};

inline bool VEditTab::getIsEditMode() const
{
    return isEditMode;
}

inline bool VEditTab::isModified() const
{
    return textEditor->isModified();
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
