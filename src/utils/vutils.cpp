#include "vutils.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

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

QString VUtils::generateImageFileName(const QString &path, const QString &title,
                                      const QString &format)
{
    Q_ASSERT(!title.isEmpty());
    QRegularExpression regExp("[^a-zA-Z0-9_]+");
    QString baseName(title.toLower());
    baseName.replace(regExp, "_");
    baseName.prepend('_');
    QString imageName = baseName + "." + format.toLower();
    QString filePath = QDir(path).filePath(imageName);
    int index = 1;

    while (QFile(filePath).exists()) {
        imageName = QString("%1_%2.%3").arg(baseName).arg(index++)
                                       .arg(format.toLower());
        filePath = QDir(path).filePath(imageName);
    }
    return imageName;
}

void VUtils::processStyle(QString &style)
{
    QVector<QPair<QString, QString> > varMap;

    // Initialize varMap
    addQssVarToMap(varMap, "base-color", "#4CAF50");
    addQssVarToMap(varMap, "hover-color", "#42A5F5");

    // Process style
    for (int i = 0; i < varMap.size(); ++i) {
        const QPair<QString, QString> &map = varMap[i];
        style.replace("@" + map.first, map.second);
    }
}

bool VUtils::isMarkdown(const QString &name)
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

QString VUtils::fileNameFromPath(const QString &path)
{
    if (path.isEmpty()) {
        return path;
    }
    return QFileInfo(QDir::cleanPath(path)).fileName();
}

QString VUtils::basePathFromPath(const QString &path)
{
    return QFileInfo(path).path();
}
