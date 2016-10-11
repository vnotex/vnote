#ifndef VEDITOR_H
#define VEDITOR_H

#include <QStackedWidget>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"
#include "vdocument.h"

class QTextBrowser;
class VEdit;
class QWebEngineView;
class HGMarkdownHighlighter;

class VEditor : public QStackedWidget
{
public:
    VEditor(const QString &path, const QString &name, bool modifiable,
            QWidget *parent = 0);
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

    VNoteFile *noteFile;
    bool isEditMode;
    QTextBrowser *textBrowser;
    VEdit *textEditor;
    QWebEngineView *webPreviewer;
    VDocument document;
    HGMarkdownHighlighter *mdHighlighter;
};

#endif // VEDITOR_H
