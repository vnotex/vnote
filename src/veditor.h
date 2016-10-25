#ifndef VEDITOR_H
#define VEDITOR_H

#include <QStackedWidget>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"
#include "vdocument.h"
#include "vmarkdownconverter.h"
#include "vconfigmanager.h"

class QTextBrowser;
class VEdit;
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

#endif // VEDITOR_H
