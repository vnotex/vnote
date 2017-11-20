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
#include <QValidator>
#include <QRegExpValidator>
#include <QRegExp>
#include <QKeySequence>

#include "vorphanfile.h"
#include "vnote.h"
#include "vnotebook.h"
#include "hgmarkdownhighlighter.h"

extern VConfigManager *g_config;

QVector<QPair<QString, QString>> VUtils::s_availableLanguages;

const QString VUtils::c_imageLinkRegExp = QString("\\!\\[([^\\]]*)\\]\\(([^\\)\"]+)\\s*(\"(\\\\.|[^\"\\)])*\")?\\s*\\)");

const QString VUtils::c_imageTitleRegExp = QString("[\\w\\(\\)@#%\\*\\-\\+=\\?<>\\,\\.\\s]*");

const QString VUtils::c_fileNameRegExp = QString("[^\\\\/:\\*\\?\"<>\\|]*");

const QString VUtils::c_fencedCodeBlockStartRegExp = QString("^(\\s*)```([^`\\s]*)\\s*[^`]*$");

const QString VUtils::c_fencedCodeBlockEndRegExp = QString("^(\\s*)```$");

const QString VUtils::c_previewImageBlockRegExp = QString("[\\n|^][ |\\t]*\\xfffc[ |\\t]*(?=\\n)");

const QString VUtils::c_headerRegExp = QString("^(#{1,6})\\s+(((\\d+\\.)+(?=\\s))?\\s?\\S.*)\\s*$");

const QString VUtils::c_headerPrefixRegExp = QString("^(#{1,6}\\s+((\\d+\\.)+(?=\\s))?\\s?)($|\\S.*\\s*$)");

void VUtils::initAvailableLanguage()
{
    if (!s_availableLanguages.isEmpty()) {
        return;
    }

    s_availableLanguages.append(QPair<QString, QString>("en_US", "English (US)"));
    s_availableLanguages.append(QPair<QString, QString>("zh_CN", "Chinese"));
}

QString VUtils::readFileFromDisk(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "fail to open file" << filePath << "to read";
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

bool VUtils::writeJsonToDisk(const QString &p_filePath, const QJsonObject &p_json)
{
    QFile file(p_filePath);
    // We use Unix LF for config file.
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to open file" << p_filePath << "to write";
        return false;
    }

    QJsonDocument doc(p_json);
    if (-1 == file.write(doc.toJson())) {
        return false;
    }

    return true;
}

QJsonObject VUtils::readJsonFromDisk(const QString &p_filePath)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "fail to open file" << p_filePath << "to read";
        return QJsonObject();
    }

    return QJsonDocument::fromJson(file.readAll()).object();
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

QString VUtils::generateImageFileName(const QString &path,
                                      const QString &title,
                                      const QString &format)
{
    QRegExp regExp("\\W");
    QString baseName(title.toLower());

    // Remove non-character chars.
    baseName.remove(regExp);

    // Constrain the length of the name.
    baseName.truncate(10);

    if (!baseName.isEmpty()) {
        baseName.prepend('_');
    }

    // Add current time and random number to make the name be most likely unique
    baseName = baseName + '_' + QString::number(QDateTime::currentDateTime().toTime_t());
    baseName = baseName + '_' + QString::number(qrand());

    QDir dir(path);
    QString imageName = baseName + "." + format.toLower();
    int index = 1;

    while (fileExists(dir, imageName, true)) {
        imageName = QString("%1_%2.%3").arg(baseName).arg(index++)
                                       .arg(format.toLower());
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
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);
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

    QVector<VElementRegion> regions = fetchImageRegionsUsingParser(text);
    QRegExp regExp(c_imageLinkRegExp);
    QString basePath = p_file->fetchBasePath();
    for (int i = 0; i < regions.size(); ++i) {
        const VElementRegion &reg = regions[i];
        QString linkText = text.mid(reg.m_startPos, reg.m_endPos - reg.m_startPos);
        bool matched = regExp.exactMatch(linkText);
        if (!matched) {
            // Image links with reference format will not match.
            continue;
        }

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

QJsonObject VUtils::clipboardToJson()
{
    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    QJsonObject obj;
    if (mimeData->hasText()) {
        QString text = mimeData->text();
        obj = QJsonDocument::fromJson(text.toUtf8()).object();
        qDebug() << "Json object in clipboard" << obj;
    }

    return obj;
}

ClipboardOpType VUtils::operationInClipboard()
{
    QJsonObject obj = clipboardToJson();
    if (obj.contains(ClipboardConfig::c_type)) {
        return (ClipboardOpType)obj[ClipboardConfig::c_type].toInt();
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

    QDir dir;
    if (!dir.mkpath(basePathFromPath(p_destFilePath))) {
        qWarning() << "fail to create directory" << basePathFromPath(p_destFilePath);
        return false;
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

bool VUtils::copyDirectory(const QString &p_srcDirPath, const QString &p_destDirPath, bool p_isCut)
{
    QString srcPath = QDir::cleanPath(p_srcDirPath);
    QString destPath = QDir::cleanPath(p_destDirPath);
    if (srcPath == destPath) {
        return true;
    }

    if (QFileInfo::exists(destPath)) {
        qWarning() << QString("target directory %1 already exists").arg(destPath);
        return false;
    }

    // QDir.rename() could not move directory across drives.

    // Make sure target directory exists.
    QDir destDir(destPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(destPath)) {
            qWarning() << QString("fail to create target directory %1").arg(destPath);
            return false;
        }
    }

    // Handle directory recursively.
    QDir srcDir(srcPath);
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

    if (p_isCut) {
        if (!destDir.rmdir(srcPath)) {
            qWarning() << QString("fail to delete source directory %1 after cut").arg(srcPath);
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
            okBtn->setStyleSheet(g_config->c_dangerBtnStyle);
        }
    }
    return msgBox.exec();
}

QString VUtils::generateCopiedFileName(const QString &p_dirPath,
                                       const QString &p_fileName,
                                       bool p_completeBaseName)
{
    QDir dir(p_dirPath);
    if (!dir.exists() || !dir.exists(p_fileName)) {
        return p_fileName;
    }

    QFileInfo fi(p_fileName);
    QString baseName = p_completeBaseName ? fi.completeBaseName() : fi.baseName();
    QString suffix = p_completeBaseName ? fi.suffix() : fi.completeSuffix();

    int index = 0;
    QString fileName;
    do {
        QString seq;
        if (index > 0) {
            seq = QString("%1").arg(QString::number(index), 3, '0');
        }

        index++;
        fileName = QString("%1_copy%2").arg(baseName).arg(seq);
        if (!suffix.isEmpty()) {
            fileName = fileName + "." + suffix;
        }
    } while (fileExists(dir, fileName, true));

    return fileName;
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
    if (s_availableLanguages.isEmpty()) {
        initAvailableLanguage();
    }

    return s_availableLanguages;
}

bool VUtils::isValidLanguage(const QString &p_lang)
{
    for (auto const &lang : getAvailableLanguages()) {
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
    QString locale = g_config->getLanguage();
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
    if (p_name.isEmpty()) {
        return DocType::Unknown;
    }

    const QHash<int, QList<QString>> &suffixes = g_config->getDocSuffixes();

    QString suf = QFileInfo(p_name).suffix().toLower();
    for (auto it = suffixes.begin(); it != suffixes.end(); ++it) {
        if (it.value().contains(suf)) {
            return DocType(it.key());
        }
    }

    return DocType::Unknown;
}

QString VUtils::generateHtmlTemplate(MarkdownConverterType p_conType, bool p_exportPdf)
{
    QString jsFile, extraFile;
    switch (p_conType) {
    case MarkdownConverterType::Marked:
        jsFile = "qrc" + VNote::c_markedJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Hoedown:
        jsFile = "qrc" + VNote::c_hoedownJsFile;
        // Use Marked to highlight code blocks.
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::MarkdownIt:
    {
        jsFile = "qrc" + VNote::c_markdownitJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markdownitExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitAnchorExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitTaskListExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitSubExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitSupExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitFootnoteExtraFile + "\"></script>\n";

        MarkdownitOption opt = g_config->getMarkdownitOption();
        QString optJs = QString("<script>var VMarkdownitOption = {"
                                "html: %1, breaks: %2, linkify: %3};"
                                "</script>\n")
                               .arg(opt.m_html ? "true" : "false")
                               .arg(opt.m_breaks ? "true" : "false")
                               .arg(opt.m_linkify ? "true" : "false");
        extraFile += optJs;
        break;
    }

    case MarkdownConverterType::Showdown:
        jsFile = "qrc" + VNote::c_showdownJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_showdownExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_showdownAnchorExtraFile + "\"></script>\n";

        break;

    default:
        Q_ASSERT(false);
    }

    if (g_config->getEnableMermaid()) {
        extraFile += "<link rel=\"stylesheet\" type=\"text/css\" href=\"qrc" + VNote::c_mermaidCssFile + "\"/>\n" +
                     "<script src=\"qrc" + VNote::c_mermaidApiJsFile + "\"></script>\n" +
                     "<script>var VEnableMermaid = true;</script>\n";
    }

    if (g_config->getEnableFlowchart()) {
        extraFile += "<script src=\"qrc" + VNote::c_raphaelJsFile + "\"></script>\n" +
                     "<script src=\"qrc" + VNote::c_flowchartJsFile + "\"></script>\n" +
                     "<script>var VEnableFlowchart = true;</script>\n";
    }

    if (g_config->getEnableMathjax()) {
        extraFile += "<script type=\"text/x-mathjax-config\">"
                     "MathJax.Hub.Config({\n"
                     "                    tex2jax: {inlineMath: [['$','$'], ['\\\\(','\\\\)']]},\n"
                     "                    showProcessingMessages: false,\n"
                     "                    messageStyle: \"none\"});\n"
                     "</script>\n"
                     "<script type=\"text/javascript\" async src=\"" + g_config->getMathjaxJavascript() + "\"></script>\n" +
                     "<script>var VEnableMathjax = true;</script>\n";
    }

    if (g_config->getEnableImageCaption()) {
        extraFile += "<script>var VEnableImageCaption = true;</script>\n";
    }

    if (g_config->getEnableCodeBlockLineNumber()) {
        extraFile += "<script src=\"qrc" + VNote::c_highlightjsLineNumberExtraFile + "\"></script>\n" +
                     "<script>var VEnableHighlightLineNumber = true;</script>\n";
    }

    QString htmlTemplate;
    if (p_exportPdf) {
        htmlTemplate = VNote::s_markdownTemplatePDF;
    } else {
        htmlTemplate = VNote::s_markdownTemplate;
    }

    htmlTemplate.replace(c_htmlJSHolder, jsFile);
    if (!extraFile.isEmpty()) {
        htmlTemplate.replace(c_htmlExtraHolder, extraFile);
    }

    return htmlTemplate;
}

QString VUtils::getFileNameWithSequence(const QString &p_directory,
                                        const QString &p_baseFileName,
                                        bool p_completeBaseName)
{
    QDir dir(p_directory);
    if (!dir.exists() || !dir.exists(p_baseFileName)) {
        return p_baseFileName;
    }

    // Append a sequence.
    QFileInfo fi(p_baseFileName);
    QString baseName = p_completeBaseName ? fi.completeBaseName() : fi.baseName();
    QString suffix = p_completeBaseName ? fi.suffix() : fi.completeSuffix();
    int seq = 1;
    QString fileName;
    do {
        fileName = QString("%1_%2").arg(baseName).arg(QString::number(seq++), 3, '0');
        if (!suffix.isEmpty()) {
            fileName = fileName + "." + suffix;
        }
    } while (fileExists(dir, fileName, true));

    return fileName;
}

QString VUtils::getDirNameWithSequence(const QString &p_directory,
                                       const QString &p_baseDirName)
{
    QDir dir(p_directory);
    if (!dir.exists() || !dir.exists(p_baseDirName)) {
        return p_baseDirName;
    }

    // Append a sequence.
    int seq = 1;
    QString fileName;
    do {
        fileName = QString("%1_%2").arg(p_baseDirName).arg(QString::number(seq++), 3, '0');
    } while (fileExists(dir, fileName, true));

    return fileName;
}

QString VUtils::getRandomFileName(const QString &p_directory)
{
    Q_ASSERT(!p_directory.isEmpty());

    QString name;
    QDir dir(p_directory);
    do {
        name = QString::number(QDateTime::currentDateTimeUtc().toTime_t());
        name = name + '_' + QString::number(qrand());
    } while (fileExists(dir, name, true));

    return name;
}

bool VUtils::checkPathLegal(const QString &p_path)
{
    // Ensure every part of the p_path is a valid file name until we come to
    // an existing parent directory.
    if (p_path.isEmpty()) {
        return false;
    }

    if (QFileInfo::exists(p_path)) {
#if defined(Q_OS_WIN)
            // On Windows, "/" and ":" will also make exists() return true.
            if (p_path.startsWith('/') || p_path == ":") {
                return false;
            }
#endif

        return true;
    }

    bool ret = false;
    int pos;
    QString basePath = basePathFromPath(p_path);
    QString fileName = fileNameFromPath(p_path);
    QValidator *validator = new QRegExpValidator(QRegExp(c_fileNameRegExp));
    while (!fileName.isEmpty()) {
        QValidator::State validFile = validator->validate(fileName, pos);
        if (validFile != QValidator::Acceptable) {
            break;
        }

        if (QFileInfo::exists(basePath)) {
            ret = true;

#if defined(Q_OS_WIN)
            // On Windows, "/" and ":" will also make exists() return true.
            if (basePath.startsWith('/') || basePath == ":") {
                ret = false;
            }
#endif

            break;
        }

        fileName = fileNameFromPath(basePath);
        basePath = basePathFromPath(basePath);
    }

    delete validator;
    return ret;
}

bool VUtils::checkFileNameLegal(const QString &p_name)
{
    if (p_name.isEmpty()) {
        return false;
    }

    QRegExp exp(c_fileNameRegExp);
    return exp.exactMatch(p_name);
}

bool VUtils::equalPath(const QString &p_patha, const QString &p_pathb)
{
    QString a = QDir::cleanPath(p_patha);
    QString b = QDir::cleanPath(p_pathb);

#if defined(Q_OS_WIN)
    a = a.toLower();
    b = b.toLower();
#endif

    return a == b;
}

bool VUtils::splitPathInBasePath(const QString &p_base,
                                 const QString &p_path,
                                 QStringList &p_parts)
{
    p_parts.clear();
    QString a = QDir::cleanPath(p_base);
    QString b = QDir::cleanPath(p_path);

#if defined(Q_OS_WIN)
    if (!b.toLower().startsWith(a.toLower())) {
        return false;
    }
#else
    if (!b.startsWith(a)) {
        return false;
    }
#endif

    if (a.size() == b.size()) {
        return true;
    }

    Q_ASSERT(a.size() < b.size());

    if (b.at(a.size()) != '/') {
        return false;
    }

    p_parts = b.right(b.size() - a.size() - 1).split("/", QString::SkipEmptyParts);

    qDebug() << QString("split path %1 based on %2 to %3 parts").arg(p_path).arg(p_base).arg(p_parts.size());
    return true;
}

void VUtils::decodeUrl(QString &p_url)
{
    QHash<QString, QString> maps;
    maps.insert("%20", " ");

    for (auto it = maps.begin(); it != maps.end(); ++it) {
        p_url.replace(it.key(), it.value());
    }
}

QString VUtils::getShortcutText(const QString &p_keySeq)
{
    return QKeySequence(p_keySeq).toString(QKeySequence::NativeText);
}

bool VUtils::deleteDirectory(const VNotebook *p_notebook,
                             const QString &p_path,
                             bool p_skipRecycleBin)
{
    if (p_skipRecycleBin) {
        QDir dir(p_path);
        return dir.removeRecursively();
    } else {
        // Move it to the recycle bin folder.
        return deleteFile(p_notebook->getRecycleBinFolderPath(), p_path);
    }
}

bool VUtils::emptyDirectory(const VNotebook *p_notebook,
                            const QString &p_path,
                            bool p_skipRecycleBin)
{
    QDir dir(p_path);
    if (!dir.exists()) {
        return true;
    }

    QFileInfoList nodes = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden
                                            | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (int i = 0; i < nodes.size(); ++i) {
        const QFileInfo &fileInfo = nodes.at(i);
        if (fileInfo.isDir()) {
            if (!deleteDirectory(p_notebook, fileInfo.absoluteFilePath(), p_skipRecycleBin)) {
                return false;
            }
        } else {
            Q_ASSERT(fileInfo.isFile());
            if (!deleteFile(p_notebook, fileInfo.absoluteFilePath(), p_skipRecycleBin)) {
                return false;
            }
        }
    }

    return true;
}

bool VUtils::deleteFile(const VNotebook *p_notebook,
                        const QString &p_path,
                        bool p_skipRecycleBin)
{
    if (p_skipRecycleBin) {
        QFile file(p_path);
        return file.remove();
    } else {
        // Move it to the recycle bin folder.
        return deleteFile(p_notebook->getRecycleBinFolderPath(), p_path);
    }
}

bool VUtils::deleteFile(const QString &p_path)
{
    QFile file(p_path);
    bool ret = file.remove();
    if (ret) {
        qDebug() << "deleted file" << p_path;
    } else {
        qWarning() << "fail to delete file" << p_path;
    }

    return ret;
}

bool VUtils::deleteFile(const VOrphanFile *p_file,
                        const QString &p_path,
                        bool p_skipRecycleBin)
{
    if (p_skipRecycleBin) {
        QFile file(p_path);
        return file.remove();
    } else {
        // Move it to the recycle bin folder.
        return deleteFile(p_file->fetchRecycleBinFolderPath(), p_path);
    }
}

static QString getRecycleBinSubFolderToUse(const QString &p_folderPath)
{
    QDir dir(p_folderPath);
    return QDir::cleanPath(dir.absoluteFilePath(QDateTime::currentDateTime().toString("yyyyMMdd")));
}

bool VUtils::deleteFile(const QString &p_recycleBinFolderPath,
                        const QString &p_path)
{
    QString binPath = getRecycleBinSubFolderToUse(p_recycleBinFolderPath);
    QDir binDir(binPath);
    if (!binDir.exists()) {
        binDir.mkpath(binPath);
        if (!binDir.exists()) {
            return false;
        }
    }

    QString destName = getFileNameWithSequence(binPath,
                                               fileNameFromPath(p_path),
                                               true);

    qDebug() << "try to move" << p_path << "to" << binPath << "as" << destName;
    if (!binDir.rename(p_path, binDir.filePath(destName))) {
        qWarning() << "fail to move" << p_path << "to" << binDir.filePath(destName);
        return false;
    }

    return true;
}

QVector<VElementRegion> VUtils::fetchImageRegionsUsingParser(const QString &p_content)
{
    Q_ASSERT(!p_content.isEmpty());
    QVector<VElementRegion> regs;

    QByteArray ba = p_content.toUtf8();
    const char *data = (const char *)ba.data();
    int len = ba.size();

    pmh_element **result = NULL;
    char *content = new char[len + 1];
    memcpy(content, data, len);
    content[len] = '\0';

    pmh_markdown_to_elements(content, pmh_EXT_NONE, &result);

    if (!result) {
        return regs;
    }

    pmh_element *elem = result[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        regs.push_back(VElementRegion(elem->pos, elem->end));

        elem = elem->next;
    }

    pmh_free_elements(result);

    return regs;
}

QString VUtils::displayDateTime(const QDateTime &p_dateTime)
{
    QString res = p_dateTime.date().toString(Qt::DefaultLocaleLongDate);
    res += " " + p_dateTime.time().toString();
    return res;
}

bool VUtils::fileExists(const QDir &p_dir, const QString &p_name, bool p_forceCaseInsensitive)
{
    if (!p_forceCaseInsensitive) {
        return p_dir.exists(p_name);
    }

    QString name = p_name.toLower();
    QStringList names = p_dir.entryList(QDir::Dirs | QDir::Files | QDir::Hidden
                                        | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    foreach (const QString &str, names) {
        if (str.toLower() == name) {
            return true;
        }
    }

    return false;
}

void VUtils::addErrMsg(QString *p_msg, const QString &p_str)
{
    if (!p_msg) {
        return;
    }

    if (p_msg->isEmpty()) {
        *p_msg = p_str;
    } else {
        *p_msg = *p_msg + '\n' + p_str;
    }
}

QStringList VUtils::filterFilePathsToOpen(const QStringList &p_files)
{
    QStringList paths;
    for (int i = 0; i < p_files.size(); ++i) {
        QString path = validFilePathToOpen(p_files[i]);
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }

    return paths;
}

QString VUtils::validFilePathToOpen(const QString &p_file)
{
    if (QFileInfo::exists(p_file)) {
        QFileInfo fi(p_file);
        if (fi.isFile()) {
            // Need to use absolute path here since VNote may be launched
            // in different working directory.
            return QDir::cleanPath(fi.absoluteFilePath());
        }
    }

    return QString();
}

bool VUtils::isControlModifierForVim(int p_modifiers)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return p_modifiers == Qt::MetaModifier;
#else
    return p_modifiers == Qt::ControlModifier;
#endif
}

void VUtils::touchFile(const QString &p_file)
{
    QFile file(p_file);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to touch file" << p_file;
        return;
    }

    file.close();
}
