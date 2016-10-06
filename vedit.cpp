#include <QtWidgets>
#include "vedit.h"

VEdit::VEdit(const QString &path, const QString &name,
             QWidget *parent) : QTextEdit(parent), path(path), name(name)
{
    fileText = readFile(QDir(path).filePath(name));
    showTextReadMode();
}

void VEdit::showTextReadMode()
{
    setText(fileText);
    setReadOnly(true);
}

QString VEdit::readFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "error: fail to read file" << filePath;
        return QString();
    }
    QString fileText(file.readAll());
    file.close();
    return fileText;
}

bool VEdit::writeFile(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "error: fail to open file" << filePath << "to write to";
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    file.close();
    return true;
}

bool VEdit::requestClose()
{
    return true;
}
