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
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QWebEngineView>
#include <QAction>
#include <QTreeWidgetItem>
#include <QFormLayout>
#include <QInputDialog>

#include "vorphanfile.h"
#include "vnote.h"
#include "vnotebook.h"
#include "hgmarkdownhighlighter.h"
#include "vpreviewpage.h"

extern VConfigManager *g_config;

QVector<QPair<QString, QString>> VUtils::s_availableLanguages;

const QString VUtils::c_imageLinkRegExp = QString("\\!\\[([^\\]]*)\\]\\(([^\\)\"'\\s]+)\\s*"
                                                  "((\"[^\"\\)\\n]*\")|('[^'\\)\\n]*'))?\\s*"
                                                  "(=(\\d*)x(\\d*))?\\s*"
                                                  "\\)");

const QString VUtils::c_imageTitleRegExp = QString("[\\w\\(\\)@#%\\*\\-\\+=\\?<>\\,\\.\\s]*");

const QString VUtils::c_fileNameRegExp = QString("[^\\\\/:\\*\\?\"<>\\|]*");

const QString VUtils::c_fencedCodeBlockStartRegExp = QString("^(\\s*)```([^`\\s]*)\\s*[^`]*$");

const QString VUtils::c_fencedCodeBlockEndRegExp = QString("^(\\s*)```$");

const QString VUtils::c_mathjaxInlineRegExp = QString("(?:^|[^\\$\\\\]|(?:^|[^\\\\])(?:\\\\\\\\)+)\\$(?!\\$)");

const QString VUtils::c_mathjaxBlockRegExp = QString("(?:^|[^\\$\\\\]|(?:^|[^\\\\])(?:\\\\\\\\)+)\\$\\$(?!\\$)");

const QString VUtils::c_previewImageBlockRegExp = QString("[\\n|^][ |\\t]*\\xfffc[ |\\t]*(?=\\n)");

const QString VUtils::c_headerRegExp = QString("^(#{1,6})\\s+(((\\d+\\.)+(?=\\s))?\\s*(\\S.*)?)$");

const QString VUtils::c_headerPrefixRegExp = QString("^(#{1,6}\\s+((\\d+\\.)+(?=\\s))?\\s*)($|(\\S.*)?$)");

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

bool VUtils::writeFileToDisk(const QString &p_filePath, const QString &p_text)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "fail to open file" << p_filePath << "to write";
        return false;
    }

    QTextStream stream(&file);
    stream << p_text;
    file.close();
    qDebug() << "write file content:" << p_filePath;
    return true;
}

bool VUtils::writeFileToDisk(const QString &p_filePath, const QByteArray &p_data)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to open file" << p_filePath << "to write";
        return false;
    }

    file.write(p_data);
    file.close();
    qDebug() << "write file content:" << p_filePath;
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

    // Used to de-duplicate the links. Url as the key.
    QSet<QString> fetchedLinks;

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
        link.m_url = imageUrl;
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
            if (!fetchedLinks.contains(link.m_url)) {
                fetchedLinks.insert(link.m_url);
                images.push_back(link);
                qDebug() << "fetch one image:" << link.m_type << link.m_path << link.m_url;
            }
        }
    }

    if (!isOpened) {
        p_file->close();
    }

    return images;
}

QString VUtils::imageLinkUrlToPath(const QString &p_basePath, const QString &p_url)
{
    QString path;
    QFileInfo info(p_basePath, p_url);
    if (info.exists()) {
        if (info.isNativePath()) {
            // Local file.
            path = QDir::cleanPath(info.absoluteFilePath());
        } else {
            path = p_url;
        }
    } else {
        path = QUrl(p_url).toString();
    }

    return path;
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

int VUtils::showMessage(QMessageBox::Icon p_icon,
                        const QString &p_title,
                        const QString &p_text,
                        const QString &p_infoText,
                        QMessageBox::StandardButtons p_buttons,
                        QMessageBox::StandardButton p_defaultBtn,
                        QWidget *p_parent,
                        MessageBoxType p_type)
{
    QMessageBox msgBox(p_icon, p_title, p_text, p_buttons, p_parent);
    msgBox.setInformativeText(p_infoText);
    msgBox.setDefaultButton(p_defaultBtn);

    if (p_type == MessageBoxType::Danger) {
        QPushButton *okBtn = dynamic_cast<QPushButton *>(msgBox.button(QMessageBox::Ok));
        if (okBtn) {
            setDynamicProperty(okBtn, "DangerBtn");
        }
    }

    QPushButton *defaultBtn = dynamic_cast<QPushButton *>(msgBox.button(p_defaultBtn));
    if (defaultBtn) {
        setDynamicProperty(defaultBtn, "SpecialBtn");
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
    static qreal factor = -1;

    if (factor < 0) {
        const qreal refDpi = 96;
        qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInch();
        factor = dpi / refDpi;
        if (factor < 1) {
            factor = 1;
        } else {
            // Keep only two digits after the dot.
            factor = (int)(factor * 100) / 100.0;
        }
    }

    return factor;
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

QString VUtils::generateSimpleHtmlTemplate(const QString &p_body)
{
    QString html(VNote::s_simpleHtmlTemplate);
    return html.replace(HtmlHolder::c_bodyHolder, p_body);
}

QString VUtils::generateHtmlTemplate(MarkdownConverterType p_conType)
{
    return generateHtmlTemplate(VNote::s_markdownTemplate, p_conType);
}

QString VUtils::generateHtmlTemplate(MarkdownConverterType p_conType,
                                     const QString &p_renderBg,
                                     const QString &p_renderStyle,
                                     const QString &p_renderCodeBlockStyle,
                                     bool p_isPDF,
                                     bool p_wkhtmltopdf,
                                     bool p_addToc)
{
    Q_ASSERT((p_isPDF && p_wkhtmltopdf) || !p_wkhtmltopdf);

    QString templ = VNote::generateHtmlTemplate(g_config->getRenderBackgroundColor(p_renderBg),
                                                g_config->getCssStyleUrl(p_renderStyle),
                                                g_config->getCodeBlockCssStyleUrl(p_renderCodeBlockStyle),
                                                p_isPDF);

    return generateHtmlTemplate(templ, p_conType, p_isPDF, p_wkhtmltopdf, p_addToc);
}

QString VUtils::generateHtmlTemplate(const QString &p_template,
                                     MarkdownConverterType p_conType,
                                     bool p_isPDF,
                                     bool p_wkhtmltopdf,
                                     bool p_addToc)
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
                    "<script src=\"qrc" + VNote::c_markdownitImsizeExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitFootnoteExtraFile + "\"></script>\n";

        const MarkdownitOption &opt = g_config->getMarkdownitOption();

        if (opt.m_sup) {
            extraFile += "<script src=\"qrc" + VNote::c_markdownitSupExtraFile + "\"></script>\n";
        }

        if (opt.m_sub) {
            extraFile += "<script src=\"qrc" + VNote::c_markdownitSubExtraFile + "\"></script>\n";
        }

        if (opt.m_metadata) {
            extraFile += "<script src=\"qrc" + VNote::c_markdownitFrontMatterExtraFile + "\"></script>\n";
        }

        if (opt.m_emoji) {
            extraFile += "<script src=\"qrc" + VNote::c_markdownitEmojiExtraFile + "\"></script>\n";
        }

        QString optJs = QString("<script>var VMarkdownitOption = {"
                                "html: %1,\n"
                                "breaks: %2,\n"
                                "linkify: %3,\n"
                                "sub: %4,\n"
                                "sup: %5,\n"
                                "metadata: %6,\n"
                                "emoji: %7 };\n"
                                "</script>\n")
                               .arg(opt.m_html ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_breaks ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_linkify ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_sub ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_sup ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_metadata ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(opt.m_emoji ? QStringLiteral("true") : QStringLiteral("false"));
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
        extraFile += "<link rel=\"stylesheet\" type=\"text/css\" href=\"" + g_config->getMermaidCssStyleUrl() + "\"/>\n" +
                     "<script src=\"qrc" + VNote::c_mermaidApiJsFile + "\"></script>\n" +
                     "<script>var VEnableMermaid = true;</script>\n";
    }

    if (g_config->getEnableFlowchart()) {
        extraFile += "<script src=\"qrc" + VNote::c_raphaelJsFile + "\"></script>\n" +
                     "<script src=\"qrc" + VNote::c_flowchartJsFile + "\"></script>\n" +
                     "<script>var VEnableFlowchart = true;</script>\n";
    }

    if (g_config->getEnableMathjax()) {
        QString mj = g_config->getMathjaxJavascript();
        if (p_wkhtmltopdf) {
            // Chante MathJax to be rendered as SVG.
            // If rendered as HTML, it will make the font of <code> messy.
            QRegExp reg("(Mathjax\\.js\\?config=)\\S+", Qt::CaseInsensitive);
            mj.replace(reg, QString("\\1%1").arg("TeX-MML-AM_SVG"));
        }

        extraFile += "<script type=\"text/x-mathjax-config\">"
                     "MathJax.Hub.Config({\n"
                     "                    tex2jax: {inlineMath: [['$','$'], ['\\\\(','\\\\)']],\n"
                                                   "processEscapes: true,\n"
                                                   "processClass: \"tex2jax_process|language-mathjax|lang-mathjax\"},\n"
                     "                    showProcessingMessages: false,\n"
                     "                    messageStyle: \"none\"});\n"
                     "</script>\n"
                     "<script type=\"text/javascript\" async src=\"" + mj + "\"></script>\n" +
                     "<script>var VEnableMathjax = true;</script>\n";

        if (p_wkhtmltopdf) {
            extraFile += "<script>var VRemoveMathjaxScript = true;</script>\n";
        }
    }

    int plantUMLMode = g_config->getPlantUMLMode();
    if (plantUMLMode != PlantUMLMode::DisablePlantUML) {
        if (plantUMLMode == PlantUMLMode::OnlinePlantUML) {
            extraFile += "<script type=\"text/javascript\" src=\"" + VNote::c_plantUMLJsFile + "\"></script>\n" +
                         "<script type=\"text/javascript\" src=\"" + VNote::c_plantUMLZopfliJsFile + "\"></script>\n" +
                         "<script>var VPlantUMLServer = '" + g_config->getPlantUMLServer() + "';</script>\n";
        }

        extraFile += QString("<script>var VPlantUMLMode = %1;</script>\n").arg(plantUMLMode);

        QString format = p_isPDF ? "png" : "svg";
        extraFile += QString("<script>var VPlantUMLFormat = '%1';</script>\n").arg(format);
    }

    if (g_config->getEnableGraphviz()) {
        extraFile += "<script>var VEnableGraphviz = true;</script>\n";

        QString format = p_isPDF ? "png" : "svg";
        extraFile += QString("<script>var VGraphvizFormat = '%1';</script>\n").arg(format);
    }

    if (g_config->getEnableImageCaption()) {
        extraFile += "<script>var VEnableImageCaption = true;</script>\n";
    }

    if (g_config->getEnableCodeBlockLineNumber()) {
        extraFile += "<script src=\"qrc" + VNote::c_highlightjsLineNumberExtraFile + "\"></script>\n" +
                     "<script>var VEnableHighlightLineNumber = true;</script>\n";
    }

    if (g_config->getEnableFlashAnchor()) {
        extraFile += "<script>var VEnableFlashAnchor = true;</script>\n";
    }

    if (p_addToc) {
        extraFile += "<script>var VAddTOC = true;</script>\n";
        extraFile += "<style type=\"text/css\">\n"
                     "    @media print {\n"
                     "        .vnote-toc {\n"
                     "            page-break-after: always;\n"
                     "         }\n"
                     "    }\n"
                     "</style>";
    }

    extraFile += "<script>var VStylesToInline = '" + g_config->getStylesToInlineWhenCopied() + "';</script>\n";

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    extraFile += "<script>var VOS = 'mac';</script>\n";
#elif defined(Q_OS_WIN)
    extraFile += "<script>var VOS = 'win';</script>\n";
#else
    extraFile += "<script>var VOS = 'linux';</script>\n";
#endif

    QString htmlTemplate(p_template);
    htmlTemplate.replace(HtmlHolder::c_JSHolder, jsFile);
    if (!extraFile.isEmpty()) {
        htmlTemplate.replace(HtmlHolder::c_extraHolder, extraFile);
    }

    return htmlTemplate;
}

QString VUtils::generateExportHtmlTemplate(const QString &p_renderBg, bool p_includeMathJax)
{
    QString templ = VNote::generateExportHtmlTemplate(g_config->getRenderBackgroundColor(p_renderBg));
    QString extra;
    if (p_includeMathJax) {
        extra += "<script type=\"text/x-mathjax-config\">\n"
                         "MathJax.Hub.Config({\n"
                             "showProcessingMessages: false,\n"
                             "messageStyle: \"none\",\n"
                             "SVG: {\n"
                                 "minScaleAdjust: 100,\n"
                                 "styles: {\n"
#if defined(Q_OS_WIN)
                                   "\".MathJax_SVG\": {\n"
                                        "\"font-size\": \"2em !important\"\n"
                                   "}\n"
#endif
                                 "}\n"
                             "}\n"
                         "});\n"
                 "</script>\n";

        QString mj = g_config->getMathjaxJavascript();
        // Chante MathJax to be rendered as SVG.
        QRegExp reg("(Mathjax\\.js\\?config=)\\S+", Qt::CaseInsensitive);
        mj.replace(reg, QString("\\1%1").arg("TeX-MML-AM_SVG"));

        extra += "<script type=\"text/javascript\" async src=\"" + mj + "\"></script>\n";
    }

    if (!extra.isEmpty()) {
        templ.replace(HtmlHolder::c_extraHolder, extra);
    }

    return templ;
}

QString VUtils::generateMathJaxPreviewTemplate()
{
    QString templ = VNote::generateMathJaxPreviewTemplate();
    templ.replace(HtmlHolder::c_JSHolder, g_config->getMathjaxJavascript());

    QString extraFile;

    QString mathjaxScale = QString::number((int)(100 * VUtils::calculateScaleFactor()));

    /*
    // Mermaid.
    extraFile += "<link rel=\"stylesheet\" type=\"text/css\" href=\"" + g_config->getMermaidCssStyleUrl() + "\"/>\n" +
                 "<script src=\"qrc" + VNote::c_mermaidApiJsFile + "\"></script>\n";
    */

    // Flowchart.
    extraFile += "<script src=\"qrc" + VNote::c_raphaelJsFile + "\"></script>\n" +
                 "<script src=\"qrc" + VNote::c_flowchartJsFile + "\"></script>\n";

    // MathJax.
    extraFile += "<script type=\"text/x-mathjax-config\">"
                 "MathJax.Hub.Config({\n"
                 "                    tex2jax: {inlineMath: [['$','$'], ['\\\\(','\\\\)']],\n"
                                               "processEscapes: true,\n"
                                               "processClass: \"tex2jax_process|language-mathjax|lang-mathjax\"},\n"
                 "                    \"HTML-CSS\": {\n"
                 "                                   scale: " + mathjaxScale + "\n"
                 "                                  },\n"
                 "                    showProcessingMessages: false,\n"
                 "                    messageStyle: \"none\"});\n"
                 "</script>\n";

    templ.replace(HtmlHolder::c_extraHolder, extraFile);

    return templ;
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

bool VUtils::deleteDirectory(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return true;
    }

    QDir dir(p_path);
    return dir.removeRecursively();
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

QString VUtils::displayDateTime(const QDateTime &p_dateTime,
                                bool p_uniformNum)
{
    QString res = p_dateTime.date().toString(p_uniformNum ? Qt::ISODate
                                                          : Qt::DefaultLocaleLongDate);
    res += " " + p_dateTime.time().toString(p_uniformNum ? Qt::ISODate
                                                         : Qt::TextDate);
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
        if (p_files[i].startsWith('-')) {
            continue;
        }

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

bool VUtils::isMetaKey(int p_key)
{
    return p_key == Qt::Key_Control
           || p_key == Qt::Key_Shift
           || p_key == Qt::Key_Meta
           || p_key == Qt::Key_Alt;
}

QComboBox *VUtils::getComboBox(QWidget *p_parent)
{
    QComboBox *box = new QComboBox(p_parent);
    QStyledItemDelegate *itemDelegate = new QStyledItemDelegate(box);
    box->setItemDelegate(itemDelegate);

    return box;
}

void VUtils::setDynamicProperty(QWidget *p_widget, const char *p_prop, bool p_val)
{
    p_widget->setProperty(p_prop, p_val);
    p_widget->style()->unpolish(p_widget);
    p_widget->style()->polish(p_widget);
}

QWebEngineView *VUtils::getWebEngineView(QWidget *p_parent)
{
    QWebEngineView *viewer = new QWebEngineView(p_parent);
    VPreviewPage *page = new VPreviewPage(viewer);
    page->setBackgroundColor(Qt::transparent);
    viewer->setPage(page);
    viewer->setZoomFactor(g_config->getWebZoomFactor());

    return viewer;
}

QString VUtils::getFileNameWithLocale(const QString &p_name, const QString &p_locale)
{
    QString locale = p_locale.isEmpty() ? getLocale() : p_locale;
    locale = locale.split('_')[0];

    QFileInfo fi(p_name);
    QString baseName = fi.completeBaseName();
    QString suffix = fi.suffix();

    if (suffix.isEmpty()) {
        return QString("%1_%2").arg(baseName).arg(locale);
    } else {
        return QString("%1_%2.%3").arg(baseName).arg(locale).arg(suffix);
    }
}

QString VUtils::getDocFile(const QString &p_name)
{
    QDir dir(VNote::c_docFileFolder);
    QString name(getFileNameWithLocale(p_name));
    if (!dir.exists(name)) {
        name = getFileNameWithLocale(p_name, "en_US");
    }

    return dir.filePath(name);
}

QString VUtils::getCaptainShortcutSequenceText(const QString &p_operation)
{
    QString capKey = g_config->getShortcutKeySequence("CaptainMode");
    QString sec = g_config->getCaptainShortcutKeySequence(p_operation);

    QKeySequence seq(capKey + "," + sec);
    if (!seq.isEmpty()) {
        return seq.toString(QKeySequence::NativeText);
    }

    return QString();
}

QString VUtils::getAvailableFontFamily(const QStringList &p_families)
{
    QStringList availFamilies = QFontDatabase().families();

    for (int i = 0; i < p_families.size(); ++i) {
        QString family = p_families[i].trimmed();
        if (family.isEmpty()) {
            continue;
        }

        for (int j = 0; j < availFamilies.size(); ++j) {
            QString availFamily = availFamilies[j];
            availFamily.remove(QRegExp("\\[.*\\]"));
            availFamily = availFamily.trimmed();
            if (family == availFamily
                || family.toLower() == availFamily.toLower()) {
                qDebug() << "matched font family" << availFamilies[j];
                return availFamilies[j];
            }
        }
    }

    return QString();
}

bool VUtils::fixTextWithShortcut(QAction *p_act, const QString &p_shortcut)
{
    QString keySeq = g_config->getShortcutKeySequence(p_shortcut);
    if (!keySeq.isEmpty()) {
        p_act->setText(QString("%1\t%2").arg(p_act->text()).arg(VUtils::getShortcutText(keySeq)));
        return true;
    }

    return false;
}

bool VUtils::fixTextWithCaptainShortcut(QAction *p_act, const QString &p_shortcut)
{
    QString keyText = VUtils::getCaptainShortcutSequenceText(p_shortcut);
    if (!keyText.isEmpty()) {
        p_act->setText(QString("%1\t%2").arg(p_act->text()).arg(keyText));
        return true;
    }

    return false;
}

QStringList VUtils::parseCombinedArgString(const QString &p_program)
{
    QStringList args;
    QString tmp;
    int quoteCount = 0;
    bool inQuote = false;

    // handle quoting. tokens can be surrounded by double quotes
    // "hello world". three consecutive double quotes represent
    // the quote character itself.
    for (int i = 0; i < p_program.size(); ++i) {
        if (p_program.at(i) == QLatin1Char('"')) {
            ++quoteCount;
            if (quoteCount == 3) {
                // third consecutive quote
                quoteCount = 0;
                tmp += p_program.at(i);
            }

            continue;
        }

        if (quoteCount) {
            if (quoteCount == 1) {
                inQuote = !inQuote;
            }

            quoteCount = 0;
        }

        if (!inQuote && p_program.at(i).isSpace()) {
            if (!tmp.isEmpty()) {
                args += tmp;
                tmp.clear();
            }
        } else {
            tmp += p_program.at(i);
        }
    }

    if (!tmp.isEmpty()) {
        args += tmp;
    }

    return args;
}

const QTreeWidgetItem *VUtils::topLevelTreeItem(const QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return NULL;
    }

    if (p_item->parent()) {
        return p_item->parent();
    } else {
        return p_item;
    }
}

QImage VUtils::imageFromFile(const QString &p_filePath)
{
    QImage img;
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "fail to open image file" << p_filePath;
        return img;
    }

    img.loadFromData(file.readAll());
    qDebug() << "imageFromFile" << p_filePath << img.isNull() << img.format();
    return img;
}

QPixmap VUtils::pixmapFromFile(const QString &p_filePath)
{
    QPixmap pixmap;
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "fail to open pixmap file" << p_filePath;
        return pixmap;
    }

    pixmap.loadFromData(file.readAll());
    qDebug() << "pixmapFromFile" << p_filePath << pixmap.isNull();
    return pixmap;
}

QFormLayout *VUtils::getFormLayout()
{
    QFormLayout *layout = new QFormLayout();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
#endif

    return layout;
}

bool VUtils::inSameDrive(const QString &p_a, const QString &p_b)
{
#if defined(Q_OS_WIN)
    QChar sep(':');
    int ai = p_a.indexOf(sep);
    int bi = p_b.indexOf(sep);

    if (ai == -1 || bi == -1) {
        return false;
    }

    return p_a.left(ai).toLower() == p_b.left(bi).toLower();
#else
    return true;
#endif
}

QString VUtils::promptForFileName(const QString &p_title,
                                  const QString &p_label,
                                  const QString &p_default,
                                  const QString &p_dir,
                                  QWidget *p_parent)
{
    QString name = p_default;
    QString text = p_label;
    QDir paDir(p_dir);
    while (true) {
        bool ok;
        name = QInputDialog::getText(p_parent,
                                     p_title,
                                     text,
                                     QLineEdit::Normal,
                                     name,
                                     &ok);
        if (!ok || name.isEmpty()) {
            return "";
        }

        if (!VUtils::checkFileNameLegal(name)) {
            text = QObject::tr("Illegal name. Please try again:");
            continue;
        }

        if (paDir.exists(name)) {
            text = QObject::tr("Name already exists. Please try again:");
            continue;
        }

        break;
    }

    return name;
}

QString VUtils::promptForFileName(const QString &p_title,
                                  const QString &p_label,
                                  const QString &p_default,
                                  std::function<bool(const QString &p_name)> p_checkExistsFunc,
                                  QWidget *p_parent)
{
    QString name = p_default;
    QString text = p_label;
    while (true) {
        bool ok;
        name = QInputDialog::getText(p_parent,
                                     p_title,
                                     text,
                                     QLineEdit::Normal,
                                     name,
                                     &ok);
        if (!ok || name.isEmpty()) {
            return "";
        }

        if (!VUtils::checkFileNameLegal(name)) {
            text = QObject::tr("Illegal name. Please try again:");
            continue;
        }

        if (p_checkExistsFunc(name)) {
            text = QObject::tr("Name already exists. Please try again:");
            continue;
        }

        break;
    }

    return name;
}

bool VUtils::onlyHasImgInHtml(const QString &p_html)
{
    // Tricky.
    return !p_html.contains("<p ");
}
