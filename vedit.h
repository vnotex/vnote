#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit VEdit(const QString &path, const QString &name, bool modifiable = false,
                   QWidget *parent = 0);
    bool requestClose();
    // Enter edit mode
    void editFile();
    // Enter read mode
    void readFile();
    // Save file
    void saveFile();

signals:

public slots:


private:
    enum class DocType { Html, Markdown };
    QString readFileFromDisk(const QString &filePath);
    bool writeFileToDisk(const QString &filePath, const QString &text);
    void showFileReadMode();
    void showFileEditMode();
    bool isMarkdown();

    QString path;
    QString name;
    QString fileText;
    DocType docType;
    bool modifiable;
    bool fileLoaded;
};

#endif // VEDIT_H
