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

private:
    bool isMarkdown(const QString &name);
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();
    void setupMarkdownPreview();
    void previewByConverter();

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

#endif // VEDITOR_H
