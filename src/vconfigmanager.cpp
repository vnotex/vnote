#include "vconfigmanager.h"
#include <QDir>
#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtDebug>
#include <QTextEdit>
#include <QStandardPaths>
#include "utils/vutils.h"
#include "vstyleparser.h"

const QString VConfigManager::orgName = QString("vnote");
const QString VConfigManager::appName = QString("vnote");
const QString VConfigManager::c_version = QString("1.4");
const QString VConfigManager::c_obsoleteDirConfigFile = QString(".vnote.json");
const QString VConfigManager::c_dirConfigFile = QString("_vnote.json");
const QString VConfigManager::defaultConfigFilePath = QString(":/resources/vnote.ini");
const QString VConfigManager::c_styleConfigFolder = QString("styles");
const QString VConfigManager::c_defaultCssFile = QString(":/resources/styles/default.css");
const QString VConfigManager::c_defaultMdhlFile = QString(":/resources/styles/default.mdhl");
const QString VConfigManager::c_solarizedDarkMdhlFile = QString(":/resources/styles/solarized-dark.mdhl");
const QString VConfigManager::c_solarizedLightMdhlFile = QString(":/resources/styles/solarized-light.mdhl");
const QString VConfigManager::c_warningTextStyle = QString("color: red; font: bold");
const QString VConfigManager::c_dataTextStyle = QString("font: bold");
const QString VConfigManager::c_dangerBtnStyle = QString("QPushButton {color: #fff; border-color: #d43f3a; background-color: #d9534f;}"
                                                         "QPushButton::hover {color: #fff; border-color: #ac2925; background-color: #c9302c;}");

VConfigManager::VConfigManager()
    : userSettings(NULL), defaultSettings(NULL)
{
}

VConfigManager::~VConfigManager()
{
    if (userSettings) {
        delete userSettings;
    }
    if (defaultSettings) {
        delete defaultSettings;
    }
}

void VConfigManager::migrateIniFile()
{
    const QString originalFolder = "tamlok";
    const QString newFolder = orgName;
    QString configFolder = getConfigFolder();
    QDir dir(configFolder);
    dir.cdUp();
    dir.rename(originalFolder, newFolder);
    userSettings->sync();
}

void VConfigManager::initialize()
{
    userSettings = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                 orgName, appName);
    defaultSettings = new QSettings(defaultConfigFilePath, QSettings::IniFormat);

    migrateIniFile();

    // Override the default css styles on start up.
    outputDefaultCssStyle();
    outputDefaultEditorStyle();

    m_editorFontSize = getConfigFromSettings("global", "editor_font_size").toInt();
    if (m_editorFontSize <= 0) {
        m_editorFontSize = 12;
    }
    baseEditFont.setPointSize(m_editorFontSize);
    baseEditPalette = QTextEdit().palette();

    m_editorStyle = getConfigFromSettings("global", "editor_style").toString();

    welcomePagePath = getConfigFromSettings("global", "welcome_page_path").toString();
    m_templateCss = getConfigFromSettings("global", "template_css").toString();
    curNotebookIndex = getConfigFromSettings("global", "current_notebook").toInt();

    markdownExtensions = hoedown_extensions(HOEDOWN_EXT_TABLES | HOEDOWN_EXT_FENCED_CODE |
                                            HOEDOWN_EXT_HIGHLIGHT | HOEDOWN_EXT_AUTOLINK |
                                            HOEDOWN_EXT_QUOTE | HOEDOWN_EXT_MATH | HOEDOWN_EXT_MATH_EXPLICIT);
    mdConverterType = (MarkdownConverterType)getConfigFromSettings("global", "markdown_converter").toInt();

    tabStopWidth = getConfigFromSettings("global", "tab_stop_width").toInt();
    isExpandTab = getConfigFromSettings("global", "is_expand_tab").toBool();
    m_highlightCursorLine = getConfigFromSettings("global", "highlight_cursor_line").toBool();
    m_highlightSelectedWord = getConfigFromSettings("global", "highlight_selected_word").toBool();
    m_highlightSearchedWord = getConfigFromSettings("global", "highlight_searched_word").toBool();
    m_autoIndent = getConfigFromSettings("global", "auto_indent").toBool();
    m_autoList = getConfigFromSettings("global", "auto_list").toBool();

    readPredefinedColorsFromSettings();
    curBackgroundColor = getConfigFromSettings("global", "current_background_color").toString();

    updateEditStyle();

    curRenderBackgroundColor = getConfigFromSettings("global",
                                                     "current_render_background_color").toString();

    m_toolsDockChecked = getConfigFromSettings("session", "tools_dock_checked").toBool();
    m_mainWindowGeometry = getConfigFromSettings("session", "main_window_geometry").toByteArray();
    m_mainWindowState = getConfigFromSettings("session", "main_window_state").toByteArray();
    m_mainSplitterState = getConfigFromSettings("session", "main_splitter_state").toByteArray();

    m_findCaseSensitive = getConfigFromSettings("global",
                                                "find_case_sensitive").toBool();
    m_findWholeWordOnly = getConfigFromSettings("global",
                                                "find_whole_word_only").toBool();
    m_findRegularExpression = getConfigFromSettings("global",
                                                    "find_regular_expression").toBool();
    m_findIncrementalSearch = getConfigFromSettings("global",
                                                    "find_incremental_search").toBool();

    m_language = getConfigFromSettings("global", "language").toString();

    m_enableMermaid = getConfigFromSettings("global", "enable_mermaid").toBool();

    m_enableMathjax = getConfigFromSettings("global", "enable_mathjax").toBool();

    m_webZoomFactor = getConfigFromSettings("global", "web_zoom_factor").toReal();
    if (!isCustomWebZoomFactor()) {
        // Calculate the zoom factor based on DPI.
        m_webZoomFactor = VUtils::calculateScaleFactor();
        qDebug() << "set WebZoomFactor to" << m_webZoomFactor;
    }

    m_enableCodeBlockHighlight = getConfigFromSettings("global",
                                                       "enable_code_block_highlight").toBool();

    m_enablePreviewImages = getConfigFromSettings("global",
                                                  "enable_preview_images").toBool();

    m_enablePreviewImageConstraint = getConfigFromSettings("global",
                                                           "enable_preview_image_constraint").toBool();

    m_enableImageConstraint = getConfigFromSettings("global",
                                                    "enable_image_constraint").toBool();

    m_enableImageCaption = getConfigFromSettings("global",
                                                 "enable_image_caption").toBool();

    m_imageFolder = getConfigFromSettings("global",
                                          "image_folder").toString();
}

void VConfigManager::readPredefinedColorsFromSettings()
{
    predefinedColors.clear();
    int size = defaultSettings->beginReadArray("predefined_colors");
    for (int i = 0; i < size; ++i) {
        defaultSettings->setArrayIndex(i);
        VColor color;
        color.name = defaultSettings->value("name").toString();
        color.rgb = defaultSettings->value("rgb").toString();
        predefinedColors.append(color);
    }
    defaultSettings->endArray();
    qDebug() << "read" << predefinedColors.size()
             << "pre-defined colors from [predefined_colors] section";
}

void VConfigManager::readNotebookFromSettings(QVector<VNotebook *> &p_notebooks, QObject *parent)
{
    Q_ASSERT(p_notebooks.isEmpty());
    int size = userSettings->beginReadArray("notebooks");
    for (int i = 0; i < size; ++i) {
        userSettings->setArrayIndex(i);
        QString name = userSettings->value("name").toString();
        QString path = userSettings->value("path").toString();
        VNotebook *notebook = new VNotebook(name, path, parent);
        notebook->readConfig();
        p_notebooks.append(notebook);
    }
    userSettings->endArray();
    qDebug() << "read" << p_notebooks.size()
             << "notebook items from [notebooks] section";
}

void VConfigManager::writeNotebookToSettings(const QVector<VNotebook *> &p_notebooks)
{
    // Clear it first
    userSettings->beginGroup("notebooks");
    userSettings->remove("");
    userSettings->endGroup();

    userSettings->beginWriteArray("notebooks");
    for (int i = 0; i < p_notebooks.size(); ++i) {
        userSettings->setArrayIndex(i);
        const VNotebook &notebook = *p_notebooks[i];
        userSettings->setValue("name", notebook.getName());
        userSettings->setValue("path", notebook.getPath());
    }
    userSettings->endArray();
    qDebug() << "write" << p_notebooks.size()
             << "notebook items in [notebooks] section";
}

QVariant VConfigManager::getConfigFromSettings(const QString &section, const QString &key) const
{
    QString fullKey = section + "/" + key;
    // First, look up the user-scoped config file
    QVariant value = userSettings->value(fullKey);
    if (!value.isNull()) {
        qDebug() << "user config:" << fullKey << value.toString();
        return value;
    }

    // Second, look up the default config file
    return getDefaultConfig(section, key);
}

void VConfigManager::setConfigToSettings(const QString &section, const QString &key, const QVariant &value)
{
    // Set the user-scoped config file
    QString fullKey = section + "/" + key;
    userSettings->setValue(fullKey, value);
    qDebug() << "set user config:" << fullKey << value.toString();
}

QVariant VConfigManager::getDefaultConfig(const QString &p_section, const QString &p_key) const
{
    QString fullKey = p_section + "/" + p_key;

    QVariant value = defaultSettings->value(fullKey);
    qDebug() << "default config:" << fullKey << value.toString();

    return value;
}

QVariant VConfigManager::resetDefaultConfig(const QString &p_section, const QString &p_key)
{
    QVariant defaultValue = getDefaultConfig(p_section, p_key);
    setConfigToSettings(p_section, p_key, defaultValue);

    return defaultValue;
}

QString VConfigManager::fetchDirConfigFilePath(const QString &p_path)
{
    QDir dir(p_path);
    QString fileName = c_dirConfigFile;

    if (dir.exists(c_obsoleteDirConfigFile)) {
        V_ASSERT(!dir.exists(c_dirConfigFile));
        if (!dir.rename(c_obsoleteDirConfigFile, c_dirConfigFile)) {
            fileName = c_obsoleteDirConfigFile;
        }
        qDebug() << "rename old directory config file:" << fileName;
    }

    QString filePath = QDir::cleanPath(dir.filePath(fileName));
    qDebug() << "use directory config file:" << filePath;
    return filePath;
}

QJsonObject VConfigManager::readDirectoryConfig(const QString &path)
{
    QString configFile = fetchDirConfigFilePath(path);

    QFile config(configFile);
    if (!config.open(QIODevice::ReadOnly)) {
        qWarning() << "fail to read directory configuration file:"
                   << configFile;
        return QJsonObject();
    }

    QByteArray configData = config.readAll();
    return QJsonDocument::fromJson(configData).object();
}

bool VConfigManager::directoryConfigExist(const QString &path)
{
     return QFileInfo::exists(fetchDirConfigFilePath(path));
}

bool VConfigManager::writeDirectoryConfig(const QString &path, const QJsonObject &configJson)
{
    QString configFile = fetchDirConfigFilePath(path);

    QFile config(configFile);
    if (!config.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to open directory configuration file for write:"
                   << configFile;
        return false;
    }

    QJsonDocument configDoc(configJson);
    config.write(configDoc.toJson());
    return true;
}

bool VConfigManager::deleteDirectoryConfig(const QString &path)
{
    QString configFile = fetchDirConfigFilePath(path);

    QFile config(configFile);
    if (!config.remove()) {
        qWarning() << "fail to delete directory configuration file:"
                   << configFile;
        return false;
    }

    qDebug() << "delete config file:" << configFile;
    return true;
}

QString VConfigManager::getLogFilePath()
{
    static QString logPath;

    if (logPath.isEmpty()) {
        QString location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        V_ASSERT(!location.isEmpty());
        QDir dir(location);
        dir.mkdir("VNote");
        logPath = dir.filePath("VNote/vnote.log");
    }

    return logPath;
}

void VConfigManager::updateMarkdownEditStyle()
{
    static const QString defaultCurrentLineBackground = "#C5CAE9";
    static const QString defaultCurrentLineVimBackground = "#A5D6A7";

    // Read style file .mdhl
    QString file(getEditorStyleUrl());

    QString styleStr = VUtils::readFileFromDisk(file);
    if (styleStr.isEmpty()) {
        return;
    }

    VStyleParser parser;
    parser.parseMarkdownStyle(styleStr);
    mdHighlightingStyles = parser.fetchMarkdownStyles(baseEditFont);
    m_codeBlockStyles = parser.fetchCodeBlockStyles(baseEditFont);
    mdEditPalette = baseEditPalette;
    mdEditFont = baseEditFont;
    QMap<QString, QMap<QString, QString>> styles;
    parser.fetchMarkdownEditorStyles(mdEditPalette, mdEditFont, styles);

    m_editorCurrentLineBackground = defaultCurrentLineBackground;
    m_editorCurrentLineVimBackground = defaultCurrentLineVimBackground;
    auto editorCurrentLineIt = styles.find("editor-current-line");
    if (editorCurrentLineIt != styles.end()) {
        auto backgroundIt = editorCurrentLineIt->find("background");
        if (backgroundIt != editorCurrentLineIt->end()) {
            m_editorCurrentLineBackground = *backgroundIt;
        }

        auto vimBackgroundIt = editorCurrentLineIt->find("vim-background");
        if (vimBackgroundIt != editorCurrentLineIt->end()) {
            m_editorCurrentLineVimBackground = "#" + *vimBackgroundIt;
        }
    }
}

void VConfigManager::updateEditStyle()
{
    static const QColor defaultColor = baseEditPalette.color(QPalette::Base);
    QColor newColor = defaultColor;
    bool force = false;
    if (curBackgroundColor != "System") {
        for (int i = 0; i < predefinedColors.size(); ++i) {
            if (predefinedColors[i].name == curBackgroundColor) {
                QString rgb = predefinedColors[i].rgb;
                if (!rgb.isEmpty()) {
                    newColor = QColor(VUtils::QRgbFromString(rgb));
                    force = true;
                }
                break;
            }
        }
    }

    baseEditPalette.setColor(QPalette::Base, newColor);

    // Update markdown editor palette
    updateMarkdownEditStyle();

    if (force) {
        mdEditPalette.setColor(QPalette::Base, newColor);
    }
}

void VConfigManager::setWebZoomFactor(qreal p_factor)
{
    if (isCustomWebZoomFactor()) {
        if (VUtils::realEqual(m_webZoomFactor, p_factor)) {
            return;
        } else if (VUtils::realEqual(p_factor, -1)) {
            m_webZoomFactor = VUtils::calculateScaleFactor();
            setConfigToSettings("global", "web_zoom_factor", -1);
            return;
        }
    } else {
        if (VUtils::realEqual(p_factor, -1)) {
            return;
        }
    }
    m_webZoomFactor = p_factor;
    setConfigToSettings("global", "web_zoom_factor", m_webZoomFactor);
}

QString VConfigManager::getConfigFolder() const
{
    V_ASSERT(userSettings);

    QString iniPath = userSettings->fileName();
    return VUtils::basePathFromPath(iniPath);
}

QString VConfigManager::getStyleConfigFolder() const
{
    return getConfigFolder() + QDir::separator() + c_styleConfigFolder;
}

QVector<QString> VConfigManager::getCssStyles() const
{
    QVector<QString> res;
    QDir dir(getStyleConfigFolder());
    if (!dir.exists()) {
        // Output pre-defined css styles to this folder.
        outputDefaultCssStyle();
    }

    // Get all the .css files in the folder.
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList("*.css"));
    QStringList files = dir.entryList();
    res.reserve(files.size());
    for (auto const &item : files) {
        res.push_back(item.left(item.size() - 4));
    }

    return res;
}

QVector<QString> VConfigManager::getEditorStyles() const
{
    QVector<QString> res;
    QDir dir(getStyleConfigFolder());
    if (!dir.exists()) {
        // Output pre-defined mdhl styles to this folder.
        outputDefaultEditorStyle();
    }

    // Get all the .mdhl files in the folder.
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList("*.mdhl"));
    QStringList files = dir.entryList();
    res.reserve(files.size());
    for (auto const &item : files) {
        res.push_back(item.left(item.size() - 5));
    }

    return res;
}

bool VConfigManager::outputDefaultCssStyle() const
{
    // Make sure the styles folder exists.
    QDir dir(getConfigFolder());
    if (!dir.exists(c_styleConfigFolder)) {
        if (!dir.mkdir(c_styleConfigFolder)) {
            return false;
        }
    }

    QString srcPath = c_defaultCssFile;
    QString destPath = getStyleConfigFolder() + QDir::separator() + QFileInfo(srcPath).fileName();

    if (QFileInfo::exists(destPath)) {
        QString bakPath = destPath + ".bak";
        // We only keep one bak file.
        if (!QFileInfo::exists(bakPath)) {
            QFile::rename(destPath, bakPath);
        } else {
            // Just delete the default style.
            QFile file(destPath);
            file.setPermissions(QFile::ReadUser | QFile::WriteUser);
            file.remove();
        }
    }

    return VUtils::copyFile(srcPath, destPath, false);
}

bool VConfigManager::outputDefaultEditorStyle() const
{
    // Make sure the styles folder exists.
    QDir dir(getConfigFolder());
    if (!dir.exists(c_styleConfigFolder)) {
        if (!dir.mkdir(c_styleConfigFolder)) {
            return false;
        }
    }

    // Always override the deafult style.
    QString srcPath = c_defaultMdhlFile;
    QString destPath = getStyleConfigFolder() + QDir::separator() + QFileInfo(srcPath).fileName();

    if (QFileInfo::exists(destPath)) {
        QString bakPath = destPath + ".bak";
        // We only keep one bak file.
        if (!QFileInfo::exists(bakPath)) {
            QFile::rename(destPath, bakPath);
        } else {
            // Just delete the default style.
            QFile file(destPath);
            file.setPermissions(QFile::ReadUser | QFile::WriteUser);
            file.remove();
        }
    }

    if (!VUtils::copyFile(srcPath, destPath, false)) {
        return false;
    }

    srcPath = c_solarizedDarkMdhlFile;
    destPath = getStyleConfigFolder() + QDir::separator() + QFileInfo(srcPath).fileName();
    if (!QFileInfo::exists(destPath)) {
        if (!VUtils::copyFile(srcPath, destPath, false)) {
            return false;
        }
    }

    srcPath = c_solarizedLightMdhlFile;
    destPath = getStyleConfigFolder() + QDir::separator() + QFileInfo(srcPath).fileName();
    if (!QFileInfo::exists(destPath)) {
        if (!VUtils::copyFile(srcPath, destPath, false)) {
            return false;
        }
    }

    return true;
}

// The URL will be used in the Web page.
QString VConfigManager::getTemplateCssUrl()
{
    QString cssPath = getStyleConfigFolder() +
                      QDir::separator() +
                      m_templateCss + ".css";
    QUrl cssUrl = QUrl::fromLocalFile(cssPath);
    cssPath = cssUrl.toString();
    if (!QFile::exists(cssUrl.toLocalFile())) {
        // Specified css not exists.
        if (m_templateCss == "default") {
            bool ret = outputDefaultCssStyle();
            if (!ret) {
                // Use embedded file.
                cssPath = "qrc" + c_defaultCssFile;
            }
        } else {
            setTemplateCss("default");
            return getTemplateCssUrl();
        }
    }

    qDebug() << "use template css:" << cssPath;
    return cssPath;
}

QString VConfigManager::getEditorStyleUrl()
{
    QString mdhlPath = getStyleConfigFolder() + QDir::separator() + m_editorStyle + ".mdhl";
    if (!QFile::exists(mdhlPath)) {
        // Specified mdhl file not exists.
        if (m_editorStyle == "default") {
            bool ret = outputDefaultEditorStyle();
            if (!ret) {
                // Use embedded file.
                mdhlPath = c_defaultMdhlFile;
            }
        } else {
            setEditorStyle("default");
            return getEditorStyleUrl();
        }
    }

    qDebug() << "use editor style:" << mdhlPath;
    return mdhlPath;

}

const QString &VConfigManager::getTemplateCss() const
{
    return m_templateCss;
}

void VConfigManager::setTemplateCss(const QString &p_css)
{
    if (m_templateCss == p_css) {
        return;
    }
    m_templateCss = p_css;
    setConfigToSettings("global", "template_css", m_templateCss);
}

const QString &VConfigManager::getEditorStyle() const
{
    return m_editorStyle;
}

void VConfigManager::setEditorStyle(const QString &p_style)
{
    if (m_editorStyle == p_style) {
        return;
    }
    m_editorStyle = p_style;
    setConfigToSettings("global", "editor_style", m_editorStyle);
    updateEditStyle();
}
