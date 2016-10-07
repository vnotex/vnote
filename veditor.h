#ifndef VEDITOR_H
#define VEDITOR_H

#include <QStackedWidget>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"

class QTextBrowser;
class VEdit;

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
    QString readFileFromDisk(const QString &filePath);
    bool writeFileToDisk(const QString &filePath, const QString &text);
    void setupUI();
    void showFileReadMode();
    void showFileEditMode();

    VNoteFile *noteFile;
    bool isEditMode;
    QTextBrowser *textBrowser;
    VEdit *textEditor;
};

#endif // VEDITOR_H
