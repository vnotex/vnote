#include "vutils.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <QRegExp>
#include <QClipboard>
#include <QApplication>
#include <QMimeData>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

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

    // Add current time to make the name be most likely unique
    baseName = baseName + '_' + QString::number(QDateTime::currentDateTime().toTime_t());
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

// Collect image links like ![](images/xx.jpg)
QVector<QString> VUtils::imagesFromMarkdownFile(const QString &filePath)
{
    Q_ASSERT(isMarkdown(filePath));
    QVector<QString> images;
    if (filePath.isEmpty()) {
        return images;
    }
    QString basePath = basePathFromPath(filePath);
    QString text = readFileFromDisk(filePath);
    QRegExp regExp("\\!\\[[^\\]]*\\]\\((images/[^/\\)]+)\\)");
    int pos = 0;

    while (pos < text.size() && (pos = regExp.indexIn(text, pos)) != -1) {
        Q_ASSERT(regExp.captureCount() == 1);
        qDebug() << regExp.capturedTexts()[0] << regExp.capturedTexts()[1];
        images.append(QDir(basePath).filePath(regExp.capturedTexts()[1]));
        pos += regExp.matchedLength();
    }
    return images;
}

void VUtils::makeDirectory(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    // mkdir will return false if it already exists
    QString basePath = basePathFromPath(path);
    QString dirName = directoryNameFromPath(path);
    QDir dir(basePath);
    if (dir.mkdir(dirName)) {
        qDebug() << "mkdir" << path;
    }
}

ClipboardOpType VUtils::opTypeInClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (mimeData->hasText()) {
        QString text = mimeData->text();
        QJsonObject clip = QJsonDocument::fromJson(text.toLocal8Bit()).object();
        if (clip.contains("operation")) {
            return (ClipboardOpType)clip["operation"].toInt();
        }
    }
    return ClipboardOpType::Invalid;
}

bool VUtils::copyFile(const QString &p_srcFilePath, const QString &p_destFilePath, bool p_isCut)
{
    QString srcPath = QDir::cleanPath(p_srcFilePath);
    QString destPath = QDir::cleanPath(p_destFilePath);

    if (srcPath == destPath) {
        return true;
    }

    if (p_isCut) {
        QFile file(srcPath);
        if (!file.rename(destPath)) {
            qWarning() << "error: fail to copy file" << srcPath << destPath;
            return false;
        }
    } else {
        if (!QFile::copy(srcPath, destPath)) {
            qWarning() << "error: fail to copy file" << srcPath << destPath;
            return false;
        }
    }
    return true;
}
