#include "vutils.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QRegExp>
#include <QClipboard>
#include <QApplication>
#include <QMimeData>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QScreen>
#include <cmath>
#include <QLocale>
#include <QPushButton>
#include <QElapsedTimer>

#include "vfile.h"

extern VConfigManager vconfig;

const QVector<QPair<QString, QString>> VUtils::c_availableLanguages = {QPair<QString, QString>("en_US", "Englisth(US)"),
                                                                       QPair<QString, QString>("zh_CN", "Chinese")};

const QString VUtils::c_imageLinkRegExp = QString("\\!\\[([^\\]]*)\\]\\(([^\\)\"]+)\\s*(\"(\\\\.|[^\"\\)])*\")?\\s*\\)");

const QString VUtils::c_fileNameRegExp = QString("[^\\\\/:\\*\\?\"<>\\|]*");

const QString VUtils::c_fencedCodeBlockStartRegExp = QString("^(\\s*)```([^`\\s]*)\\s*[^`]*$");

const QString VUtils::c_fencedCodeBlockEndRegExp = QString("^(\\s*)```$");

VUtils::VUtils()
{
}

QString VUtils::readFileFromDisk(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "fail to read file" << filePath;
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
        qWarning() << "fail to open file" << filePath << "to write";
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
    qWarning() << "fail to construct QRgb from string" << str;
    return QRgb();
}

QString VUtils::generateImageFileName(const QString &path, const QString &title,
                                      const QString &format)
{
    Q_ASSERT(!title.isEmpty());
    QRegExp regExp("\\W");
    QString baseName(title.toLower());
    baseName.remove(regExp);
    baseName.prepend('_');

    // Add current time and random number to make the name be most likely unique
    baseName = baseName + '_' + QString::number(QDateTime::currentDateTime().toTime_t());
    baseName = baseName + '_' + QString::number(qrand());

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

void VUtils::processStyle(QString &style, const QVector<QPair<QString, QString> > &varMap)
{
    // Process style
    for (int i = 0; i < varMap.size(); ++i) {
        const QPair<QString, QString> &map = varMap[i];
        style.replace("@" + map.first, map.second);
    }
}

QString VUtils::fileNameFromPath(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return p_path;
    }

    return QFileInfo(QDir::cleanPath(p_path)).fileName();
}

QString VUtils::basePathFromPath(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return p_path;
    }

    return QFileInfo(QDir::cleanPath(p_path)).path();
}

QVector<ImageLink> VUtils::fetchImagesFromMarkdownFile(VFile *p_file,
                                                       ImageLink::ImageLinkType p_type)
{
    V_ASSERT(p_file->getDocType() == DocType::Markdown);
    QVector<ImageLink> images;

    bool isOpened = p_file->isOpened();
    if (!isOpened && !p_file->open()) {
        return images;
    }

    const QString &text = p_file->getContent();
    if (text.isEmpty()) {
        if (!isOpened) {
            p_file->close();
        }

        return images;
    }

    QRegExp regExp(c_imageLinkRegExp);
    QString basePath = p_file->retriveBasePath();
    int pos = 0;
    while (pos < text.size() && (pos = regExp.indexIn(text, pos)) != -1) {
        QString imageUrl = regExp.capturedTexts()[2].trimmed();

        ImageLink link;
        QFileInfo info(basePath, imageUrl);
        if (info.exists()) {
            if (info.isNativePath()) {
                // Local file.
                link.m_path = QDir::cleanPath(info.absoluteFilePath());

                if (QDir::isRelativePath(imageUrl)) {
                    link.m_type = p_file->isInternalImageFolder(VUtils::basePathFromPath(link.m_path)) ?
                                  ImageLink::LocalRelativeInternal : ImageLink::LocalRelativeExternal;
                } else {
                    link.m_type = ImageLink::LocalAbsolute;
                }
            } else {
                link.m_type = ImageLink::Resource;
                link.m_path = imageUrl;
            }
        } else {
            QUrl url(imageUrl);
            link.m_path = url.toString();
            link.m_type = ImageLink::Remote;
        }

        if (link.m_type & p_type) {
            images.push_back(link);
            qDebug() << "fetch one image:" << link.m_type << link.m_path;
        }

        pos += regExp.matchedLength();
    }

    if (!isOpened) {
        p_file->close();
    }

    return images;
}

bool VUtils::makePath(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return true;
    }

    bool ret = true;
    QDir dir;
    if (dir.mkpath(p_path)) {
        qDebug() << "make path" << p_path;
    } else {
        qWarning() << "fail to make path" << p_path;
        ret = false;
    }

    return ret;
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
            qWarning() << "fail to copy file" << srcPath << destPath;
            return false;
        }
    } else {
        if (!QFile::copy(srcPath, destPath)) {
            qWarning() << "fail to copy file" << srcPath << destPath;
            return false;
        }
    }
    return true;
}

// Copy @p_srcDirPath to be @p_destDirPath.
bool VUtils::copyDirectory(const QString &p_srcDirPath, const QString &p_destDirPath, bool p_isCut)
{
    QString srcPath = QDir::cleanPath(p_srcDirPath);
    QString destPath = QDir::cleanPath(p_destDirPath);
    if (srcPath == destPath) {
        return true;
    }

    // Make a directory
    QDir parentDir(VUtils::basePathFromPath(p_destDirPath));
    QString dirName = VUtils::fileNameFromPath(p_destDirPath);
    if (!parentDir.mkdir(dirName)) {
        qWarning() << QString("fail to create target directory %1: already exists").arg(p_destDirPath);
        return false;
    }

    // Handle sub-dirs recursively and copy files.
    QDir srcDir(p_srcDirPath);
    QDir destDir(p_destDirPath);
    Q_ASSERT(srcDir.exists() && destDir.exists());
    QFileInfoList nodes = srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden
                                               | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (int i = 0; i < nodes.size(); ++i) {
        const QFileInfo &fileInfo = nodes.at(i);
        QString name = fileInfo.fileName();
        if (fileInfo.isDir()) {
            if (!copyDirectory(srcDir.filePath(name), destDir.filePath(name), p_isCut)) {
                return false;
            }
        } else {
            Q_ASSERT(fileInfo.isFile());
            if (!copyFile(srcDir.filePath(name), destDir.filePath(name), p_isCut)) {
                return false;
            }
        }
    }

    // Delete the src dir if p_isCut
    if (p_isCut) {
        if (!srcDir.removeRecursively()) {
            qWarning() << "fail to remove directory" << p_srcDirPath;
            return false;
        }
    }
    return true;
}

int VUtils::showMessage(QMessageBox::Icon p_icon, const QString &p_title, const QString &p_text, const QString &p_infoText,
                        QMessageBox::StandardButtons p_buttons, QMessageBox::StandardButton p_defaultBtn, QWidget *p_parent,
                        MessageBoxType p_type)
{
    QMessageBox msgBox(p_icon, p_title, p_text, p_buttons, p_parent);
    msgBox.setInformativeText(p_infoText);
    msgBox.setDefaultButton(p_defaultBtn);

    if (p_type == MessageBoxType::Danger) {
        QPushButton *okBtn = dynamic_cast<QPushButton *>(msgBox.button(QMessageBox::Ok));
        if (okBtn) {
            okBtn->setStyleSheet(vconfig.c_dangerBtnStyle);
        }
    }
    return msgBox.exec();
}

QString VUtils::generateCopiedFileName(const QString &p_dirPath, const QString &p_fileName)
{
    QString suffix;
    QString base = p_fileName;
    int dotIdx = p_fileName.lastIndexOf('.');
    if (dotIdx != -1) {
        // .md
        suffix = p_fileName.right(p_fileName.size() - dotIdx);
        base = p_fileName.left(dotIdx);
    }
    QDir dir(p_dirPath);
    QString name = p_fileName;
    QString filePath = dir.filePath(name);
    int index = 0;
    while (QFile(filePath).exists()) {
        QString seq;
        if (index > 0) {
            seq = QString::number(index);
        }
        index++;
        name = QString("%1_copy%2%3").arg(base).arg(seq).arg(suffix);
        filePath = dir.filePath(name);
    }
    return name;
}

QString VUtils::generateCopiedDirName(const QString &p_parentDirPath, const QString &p_dirName)
{
    QDir dir(p_parentDirPath);
    QString name = p_dirName;
    QString dirPath = dir.filePath(name);
    int index = 0;
    while (QDir(dirPath).exists()) {
        QString seq;
        if (index > 0) {
            seq = QString::number(index);
        }
        index++;
        name = QString("%1_copy%2").arg(p_dirName).arg(seq);
        dirPath = dir.filePath(name);
    }
    return name;
}

const QVector<QPair<QString, QString>>& VUtils::getAvailableLanguages()
{
    return c_availableLanguages;
}

bool VUtils::isValidLanguage(const QString &p_lang)
{
    for (auto const &lang : c_availableLanguages) {
        if (lang.first == p_lang) {
            return true;
        }
    }
    return false;
}

bool VUtils::isImageURL(const QUrl &p_url)
{
    QString urlStr;
    if (p_url.isLocalFile()) {
        urlStr = p_url.toLocalFile();
    } else {
        urlStr = p_url.toString();
    }
    return isImageURLText(urlStr);
}

bool VUtils::isImageURLText(const QString &p_url)
{
    QFileInfo info(p_url);
    return QImageReader::supportedImageFormats().contains(info.suffix().toLower().toLatin1());
}

qreal VUtils::calculateScaleFactor()
{
    // const qreal refHeight = 1152;
    // const qreal refWidth = 2048;
    const qreal refDpi = 96;

    qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInch();
    qreal factor = dpi / refDpi;
    return factor < 1 ? 1 : factor;
}

bool VUtils::realEqual(qreal p_a, qreal p_b)
{
    return std::abs(p_a - p_b) < 1e-8;
}

QChar VUtils::keyToChar(int p_key)
{
    if (p_key >= Qt::Key_A && p_key <= Qt::Key_Z) {
        return QChar('a' + p_key - Qt::Key_A);
    }
    return QChar();
}

QString VUtils::getLocale()
{
    QString locale = vconfig.getLanguage();
    if (locale == "System" || !isValidLanguage(locale)) {
        locale = QLocale::system().name();
    }
    return locale;
}

void VUtils::sleepWait(int p_milliseconds)
{
    if (p_milliseconds <= 0) {
        return;
    }

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < p_milliseconds) {
        QCoreApplication::processEvents();
    }
}

DocType VUtils::docTypeFromName(const QString &p_name)
{
    static QMap<int, QVector<QString>> suffixes;

    if (suffixes.isEmpty()) {
        suffixes[(int)DocType::Markdown] = {"md", "markdown", "mkd"};
        suffixes[(int)DocType::List] = {"ls", "list"};
        suffixes[(int)DocType::Container] = {"co", "container", "con"};
    }

    QString suf = QFileInfo(p_name).suffix().toLower();
    for (auto it = suffixes.begin(); it != suffixes.end(); ++it) {
        if (it.value().contains(suf)) {
            return DocType(it.key());
        }
    }

    return DocType::Html;
}
