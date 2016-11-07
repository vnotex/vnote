#ifndef VEDITTAB_H
#define VEDITTAB_H

#include <QStackedWidget>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"
#include "vdocument.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"
#include "vedit.h"
#include "vtoc.h"

class QTextBrowser;
class QWebEngineView;
class VNote;
class QXmlStreamReader;

class VEditTab : public QStackedWidget
{
    Q_OBJECT
public:
    VEditTab(const QString &path, bool modifiable, QWidget *parent = 0);
    ~VEditTab();
    bool requestClose();
    // Enter edit mode
    void editFile();
    // Enter read mode
    void readFile();
    // Save file
    bool saveFile();

    inline bool getIsEditMode() const;
    inline bool isModified() const;
    void focusTab();
    const VToc& getOutline() const;
    void scrollToAnchor(const VAnchor& anchor);

signals:
    void getFocused();
    void outlineChanged(const VToc &toc);

private slots:
    void handleFocusChanged(QWidget *old, QWidget *now);
    void updateTocFromHtml(const QString &tocHtml);

private:
    bool isMarkdown(const QString &name);
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();
    void setupMarkdownPreview();
    void previewByConverter();
    inline bool isChild(QObject *obj);
    void parseTocUl(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);
    void parseTocLi(QXmlStreamReader &xml, QVector<VHeader> &headers, int level);

    VNoteFile *noteFile;
    bool isEditMode;
    QTextBrowser *textBrowser;
    VEdit *textEditor;
    QWebEngineView *webPreviewer;
    VDocument document;
    MarkdownConverterType mdConverterType;
    VToc tableOfContent;
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

#endif // VEDITTAB_H
