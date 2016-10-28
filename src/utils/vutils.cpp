#include "vutils.h"
#include <QFile>
#include <QDebug>

VUtils::VUtils()
{
}

QString VUtils::readFileFromDisk(const QString &filePath)
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

bool VUtils::writeFileToDisk(const QString &filePath, const QString &text)
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

QRgb VUtils::QRgbFromString(const QString &str)
{
    Q_ASSERT(str.length() == 6);
    QString rStr = str.left(2);
    QString gStr = str.mid(2, 2);
    QString bStr = str.right(2);

    bool ok, ret = true;
    int red = rStr.toInt(&ok, 16);
    ret = ret && ok;
    int green = gStr.toInt(&ok, 16);
    ret = ret && ok;
    int blue = bStr.toInt(&ok, 16);
    ret = ret && ok;

    if (ret) {
        return qRgb(red, green, blue);
    }
    qWarning() << "error: fail to construct QRgb from string" << str;
    return QRgb();
}
