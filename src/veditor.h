#ifndef VEDITOR_H
#define VEDITOR_H

#include <QStackedWidget>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"
#include "vdocument.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"
#include "vedit.h"

class QTextBrowser;
class QWebEngineView;
class VNote;

class VEditor : public QStackedWidget
{
    Q_OBJECT
public:
    VEditor(const QString &path, bool modifiable, QWidget *parent = 0);
    ~VEditor();
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

signals:
    void getFocused();

private slots:
    void handleFocusChanged(QWidget *old, QWidget *now);

private:
    bool isMarkdown(const QString &name);
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();
    void setupMarkdownPreview();
    void previewByConverter();
    inline bool isChild(QObject *obj);

    VNoteFile *noteFile;
    bool isEditMode;
    QTextBrowser *textBrowser;
    VEdit *textEditor;
    QWebEngineView *webPreviewer;
    VDocument document;
    MarkdownConverterType mdConverterType;
};

inline bool VEditor::getIsEditMode() const
{
    return isEditMode;
}

inline bool VEditor::isModified() const
{
    return textEditor->isModified();
}

inline bool VEditor::isChild(QObject *obj)
{
    while (obj) {
        if (obj == this) {
            return true;
        }
        obj = obj->parent();
    }
    return false;
}

#endif // VEDITOR_H
