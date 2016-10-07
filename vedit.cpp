#include <QtWidgets>
#include "vedit.h"

VEdit::VEdit(const QString &path, const QString &name, bool modifiable,
             QWidget *parent)
    : QTextEdit(parent), path(path), name(name), modifiable(modifiable)
{
    docType = isMarkdown() ? DocType::Markdown : DocType::Html;
    fileText = readFileFromDisk(QDir(path).filePath(name));
    showFileReadMode();
    fileLoaded = true;
    qDebug() << "VEdit:" << name << (docType == DocType::Markdown ? "Markdown" : "Html");
}

void VEdit::showFileReadMode()
{
    setReadOnly(true);
    switch (docType) {
    case DocType::Html:
        if (!fileLoaded) {
            setHtml(fileText);
        }
        break;
    case DocType::Markdown:
        setText(fileText);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(docType);
    }
}

void VEdit::showFileEditMode()
{
    setReadOnly(false);
    switch (docType) {
    case DocType::Html:
        if (!fileLoaded) {
            setHtml(fileText);
        }
        break;
    case DocType::Markdown:
        setText(fileText);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(docType);
    }
}

QString VEdit::readFileFromDisk(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "error: fail to read file" << filePath;
        return QString();
    }
    QString fileText(file.readAll());
    file.close();
    qDebug() << "read file content:" << filePath;
    return fileText;
}

bool VEdit::writeFileToDisk(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "error: fail to open file" << filePath << "to write to";
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    file.close();
    qDebug() << "write file content:" << filePath;
    return true;
}

bool VEdit::requestClose()
{
    readFile();
    return isReadOnly();
}

void VEdit::editFile()
{
    if (!modifiable || !isReadOnly()) {
        return;
    }
    showFileEditMode();
}

void VEdit::readFile()
{
    if (isReadOnly()) {
        return;
    }
    if (document()->isModified()) {
        QMessageBox msgBox(QMessageBox::Information, tr("Exit edit mode"),
                           QString("Note has been changed. Do you want to save it before exit?"));
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::No | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        int ret = msgBox.exec();
        switch (ret) {
        case QMessageBox::Save:
            saveFile();
            // Fall through
        case QMessageBox::No:
            showFileReadMode();
            break;
        case QMessageBox::Cancel:
            break;
        default:
            qWarning() << "error: wrong return value from QMessageBox:" << ret;
        }
    } else {
        showFileReadMode();
    }
}

void VEdit::saveFile()
{
    if (!modifiable || !document()->isModified()) {
        return;
    }

    switch (docType) {
    case DocType::Html:
        fileText = toHtml();
        writeFileToDisk(QDir(path).filePath(name), fileText);
        break;
    case DocType::Markdown:

        break;
    default:
        qWarning() << "error: unknown doc type" << int(docType);
    }

    document()->setModified(false);
}

bool VEdit::isMarkdown()
{
    const QVector<QString> mdPostfix({"md", "markdown", "mkd"});

    QStringList list = name.split('.', QString::SkipEmptyParts);
    if (list.isEmpty()) {
        return false;
    }
    const QString &postfix = list.last();
    for (int i = 0; i < mdPostfix.size(); ++i) {
        if (postfix == mdPostfix[i]) {
            return true;
        }
    }
    return false;
}
