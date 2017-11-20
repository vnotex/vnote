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
#include <QCoreApplication>
#include "utils/vutils.h"
#include "vstyleparser.h"

const QString VConfigManager::orgName = QString("vnote");

const QString VConfigManager::appName = QString("vnote");

const QString VConfigManager::c_version = QString("1.10");

const QString VConfigManager::c_obsoleteDirConfigFile = QString(".vnote.json");

const QString VConfigManager::c_dirConfigFile = QString("_vnote.json");

const QString VConfigManager::c_defaultConfigFilePath = QString(":/resources/vnote.ini");

const QString VConfigManager::c_defaultConfigFile = QString("vnote.ini");

const QString VConfigManager::c_sessionConfigFile = QString("session.ini");

const QString VConfigManager::c_snippetConfigFile = QString("snippet.json");

const QString VConfigManager::c_styleConfigFolder = QString("styles");

const QString VConfigManager::c_codeBlockStyleConfigFolder = QString("codeblock_styles");

const QString VConfigManager::c_templateConfigFolder = QString("templates");

const QString VConfigManager::c_snippetConfigFolder = QString("snippets");

const QString VConfigManager::c_defaultCssFile = QString(":/resources/styles/default.css");

const QString VConfigManager::c_defaultCodeBlockCssFile = QString(":/utils/highlightjs/styles/vnote.css");

const QString VConfigManager::c_defaultMdhlFile = QString(":/resources/styles/default.mdhl");

const QString VConfigManager::c_solarizedDarkMdhlFile = QString(":/resources/styles/solarized-dark.mdhl");

const QString VConfigManager::c_solarizedLightMdhlFile = QString(":/resources/styles/solarized-light.mdhl");

const QString VConfigManager::c_warningTextStyle = QString("color: red; font: bold");

const QString VConfigManager::c_dataTextStyle = QString("font: bold");

const QString VConfigManager::c_dangerBtnStyle = QString("QPushButton {color: #fff; border-color: #d43f3a; background-color: #d9534f;}"
                                                         "QPushButton::hover {color: #fff; border-color: #ac2925; background-color: #c9302c;}");

const QString VConfigManager::c_vnoteNotebookFolderName = QString("vnote_notebooks");

VConfigManager::VConfigManager(QObject *p_parent)
    : QObject(p_parent),
      userSettings(NULL),
      defaultSettings(NULL),
      m_sessionSettings(NULL)
{
}

void VConfigManager::initialize()
{
    initSettings();

    // Override the default css styles on start up.
    outputDefaultCssStyle();
    outputDefaultCodeBlockCssStyle();
    outputDefaultEditorStyle();

    m_defaultEditPalette = QTextEdit().palette();

    m_editorStyle = getConfigFromSettings("global", "editor_style").toString();

    welcomePagePath = getConfigFromSettings("global", "welcome_page_path").toString();
    m_templateCss = getConfigFromSettings("global", "template_css").toString();
    m_templateCodeBlockCss = getConfigFromSettings("global", "template_code_block_css").toString();
    m_templateCodeBlockCssUrl = getConfigFromSettings("global", "template_code_block_css_url").toString();

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

    m_toolsDockChecked = getConfigFromSettings("global", "tools_dock_checked").toBool();

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

    m_enableFlowchart = getConfigFromSettings("global", "enable_flowchart").toBool();

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

    m_imageFolderExt = getConfigFromSettings("global",
                                             "external_image_folder").toString();

    m_attachmentFolder = getConfigFromSettings("global",
                                               "attachment_folder").toString();
    if (m_attachmentFolder.isEmpty()) {
        // Reset the default folder.
        m_attachmentFolder = resetDefaultConfig("global", "attachment_folder").toString();
    }

    m_enableTrailingSpaceHighlight = getConfigFromSettings("global",
                                                           "enable_trailing_space_highlight").toBool();

    m_enableVimMode = getConfigFromSettings("global",
                                            "enable_vim_mode").toBool();

    m_enableSmartImInVimMode = getConfigFromSettings("global",
                                                     "enable_smart_im_in_vim_mode").toBool();

    m_editorLineNumber = getConfigFromSettings("global",
                                               "editor_line_number").toInt();

    m_minimizeToSystemTray = getConfigFromSettings("global",
                                                   "minimize_to_system_tray").toInt();
    if (m_minimizeToSystemTray > 1 || m_minimizeToSystemTray < -1) {
        setMinimizeToSystemTray(0);
    }

    readShortcutsFromSettings();

    readCaptainShortcutsFromSettings();

    initDocSuffixes();

    m_markdownHighlightInterval = getConfigFromSettings("global",
                                                        "markdown_highlight_interval").toInt();

    m_lineDistanceHeight = getConfigFromSettings("global",
                                                 "line_distance_height").toInt();

    m_insertTitleFromNoteName = getConfigFromSettings("global",
                                                      "insert_title_from_note_name").toBool();

    int openMode = getConfigFromSettings("global",
                                         "note_open_mode").toInt();
    if (openMode == 1) {
        m_noteOpenMode = OpenFileMode::Edit;
    } else {
        m_noteOpenMode = OpenFileMode::Read;
    }

    int tmpHeadingSequenceType = getConfigFromSettings("global",
                                                       "heading_sequence_type").toInt();
    if (tmpHeadingSequenceType < (int)HeadingSequenceType::Invalid
        && tmpHeadingSequenceType >= (int)HeadingSequenceType::Disabled) {
        m_headingSequenceType = (HeadingSequenceType)tmpHeadingSequenceType;
    } else {
        m_headingSequenceType = HeadingSequenceType::Disabled;
    }

    m_headingSequenceBaseLevel = getConfigFromSettings("global",
                                                       "heading_sequence_base_level").toInt();

    m_colorColumn = getConfigFromSettings("global", "color_column").toInt();

    m_enableCodeBlockLineNumber = getConfigFromSettings("global",
                                                        "enable_code_block_line_number").toBool();

    m_toolBarIconSize = getConfigFromSettings("global",
                                              "tool_bar_icon_size").toInt();

    m_markdownitOptHtml = getConfigFromSettings("global",
                                                "markdownit_opt_html").toBool();

    m_markdownitOptBreaks = getConfigFromSettings("global",
                                                  "markdownit_opt_breaks").toBool();

    m_markdownitOptLinkify = getConfigFromSettings("global",
                                                   "markdownit_opt_linkify").toBool();

    m_recycleBinFolder = getConfigFromSettings("global",
                                               "recycle_bin_folder").toString();

    m_recycleBinFolderExt = getConfigFromSettings("global",
                                                  "external_recycle_bin_folder").toString();

    m_confirmImagesCleanUp = getConfigFromSettings("global",
                                                   "confirm_images_clean_up").toBool();

    m_confirmReloadFolder = getConfigFromSettings("global",
                                                  "confirm_reload_folder").toBool();

    m_mathjaxJavascript = getConfigFromSettings("web",
                                                "mathjax_javascript").toString();

    m_doubleClickCloseTab = getConfigFromSettings("global",
                                                  "double_click_close_tab").toBool();

    m_enableCompactMode = getConfigFromSettings("global",
                                                "enable_compact_mode").toBool();

    int tmpStartupPageMode = getConfigFromSettings("global",
                                                   "startup_page_type").toInt();
    if (tmpStartupPageMode < (int)StartupPageType::Invalid
        && tmpStartupPageMode >= (int)StartupPageType::None) {
        m_startupPageType = (StartupPageType)tmpStartupPageMode;
    } else {
        m_startupPageType = StartupPageType::None;
    }

    m_startupPages = getConfigFromSettings("global",
                                           "startup_pages").toStringList();

    initFromSessionSettings();

    m_fileTimerInterval = getConfigFromSettings("global",
                                                "file_timer_interval").toInt();
    if (m_fileTimerInterval < 100) {
        m_fileTimerInterval = 100;
    }

    m_backupDirectory = getConfigFromSettings("global",
                                              "backup_directory").toString();

    m_backupExtension = getConfigFromSettings("global",
                                              "backup_extension").toString();
    if (m_backupExtension.isEmpty()) {
        m_backupExtension = ".";
    }

    m_enableBackupFile = getConfigFromSettings("global",
                                               "enable_backup_file").toBool();

    m_vimExemptionKeys = getConfigFromSettings("global",
                                               "vim_exemption_keys").toString();
}

void VConfigManager::initSettings()
{
    Q_ASSERT(!userSettings && !defaultSettings && !m_sessionSettings);

    const char *codecForIni = "UTF-8";

    // vnote.ini.
    // First try to read vnote.ini from the directory of the executable.
    QString userIniPath = QDir(QCoreApplication::applicationDirPath()).filePath(c_defaultConfigFile);
    if (QFileInfo::exists(userIniPath)) {
        userSettings = new QSettings(userIniPath,
                                     QSettings::IniFormat,
                                     this);
    } else {
        userSettings = new QSettings(QSettings::IniFormat,
                                     QSettings::UserScope,
                                     orgName,
                                     appName,
                                     this);
    }

    userSettings->setIniCodec(codecForIni);

    qDebug() << "use user config" << userSettings->fileName();

    // Default vnote.ini from resource file.
    defaultSettings = new QSettings(c_defaultConfigFilePath, QSettings::IniFormat, this);
    defaultSettings->setIniCodec(codecForIni);

    // session.ini.
    m_sessionSettings = new QSettings(QDir(getConfigFolder()).filePath(c_sessionConfigFile),
                                      QSettings::IniFormat,
                                      this);
    m_sessionSettings->setIniCodec(codecForIni);
}

void VConfigManager::initFromSessionSettings()
{
    curNotebookIndex = getConfigFromSessionSettings("global", "current_notebook").toInt();

    m_mainWindowGeometry = getConfigFromSessionSettings("geometry",
                                                        "main_window_geometry").toByteArray();

    m_mainWindowState = getConfigFromSessionSettings("geometry",
                                                     "main_window_state").toByteArray();

    m_mainSplitterState = getConfigFromSessionSettings("geometry",
                                                       "main_splitter_state").toByteArray();

    m_naviSplitterState = getConfigFromSessionSettings("geometry",
                                                       "navi_splitter_state").toByteArray();
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

void VConfigManager::readNotebookFromSettings(QSettings *p_settings,
                                              QVector<VNotebook *> &p_notebooks,
                                              QObject *parent)
{
    Q_ASSERT(p_notebooks.isEmpty());
    int size = p_settings->beginReadArray("notebooks");
    for (int i = 0; i < size; ++i) {
        p_settings->setArrayIndex(i);
        QString name = p_settings->value("name").toString();
        QString path = p_settings->value("path").toString();
        VNotebook *notebook = new VNotebook(name, path, parent);
        notebook->readConfigNotebook();
        p_notebooks.append(notebook);
    }

    p_settings->endArray();
    qDebug() << "read" << p_notebooks.size()
             << "notebook items from [notebooks] section";
}

void VConfigManager::writeNotebookToSettings(QSettings *p_settings,
                                             const QVector<VNotebook *> &p_notebooks)
{
    // Clear it first
    p_settings->beginGroup("notebooks");
    p_settings->remove("");
    p_settings->endGroup();

    p_settings->beginWriteArray("notebooks");
    for (int i = 0; i < p_notebooks.size(); ++i) {
        p_settings->setArrayIndex(i);
        const VNotebook &notebook = *p_notebooks[i];
        p_settings->setValue("name", notebook.getName());
        p_settings->setValue("path", notebook.getPath());
    }

    p_settings->endArray();
    qDebug() << "write" << p_notebooks.size()
             << "notebook items in [notebooks] section";
}

static QVariant getConfigFromSettingsBySectionKey(const QSettings *p_settings,
                                                  const QString &p_section,
                                                  const QString &p_key)
{
    QString fullKey = p_section + "/" + p_key;
    return p_settings->value(fullKey);
}

static void setConfigToSettingsBySectionKey(QSettings *p_settings,
                                            const QString &p_section,
                                            const QString &p_key,
                                            const QVariant &p_value)
{
    QString fullKey = p_section + "/" + p_key;
    return p_settings->setValue(fullKey, p_value);
}

QVariant VConfigManager::getConfigFromSettings(const QString &section, const QString &key) const
{
    // First, look up the user-scoped config file
    QVariant value = getConfigFromSettingsBySectionKey(userSettings, section, key);
    if (!value.isNull()) {
        qDebug() << "user config:" << (section + "/" +  key) << value;
        return value;
    }

    // Second, look up the default config file
    return getDefaultConfig(section, key);
}

void VConfigManager::setConfigToSettings(const QString &section, const QString &key, const QVariant &value)
{
    // Set the user-scoped config file
    setConfigToSettingsBySectionKey(userSettings, section, key, value);
    qDebug() << "set user config:" << (section + "/" + key) << value;
}

QVariant VConfigManager::getDefaultConfig(const QString &p_section, const QString &p_key) const
{
    QVariant value = getConfigFromSettingsBySectionKey(defaultSettings, p_section, p_key);
    qDebug() << "default config:" << (p_section + "/" + p_key) << value;
    return value;
}

QVariant VConfigManager::resetDefaultConfig(const QString &p_section, const QString &p_key)
{
    QVariant defaultValue = getDefaultConfig(p_section, p_key);
    setConfigToSettings(p_section, p_key, defaultValue);

    return defaultValue;
}

QVariant VConfigManager::getConfigFromSessionSettings(const QString &p_section,
                                                      const QString &p_key) const
{
    return getConfigFromSettingsBySectionKey(m_sessionSettings,
                                             p_section,
                                             p_key);
}

void VConfigManager::setConfigToSessionSettings(const QString &p_section,
                                                const QString &p_key,
                                                const QVariant &p_value)
{
    setConfigToSettingsBySectionKey(m_sessionSettings,
                                    p_section,
                                    p_key,
                                    p_value);
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
    // We use Unix LF for config file.
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
    static const QString defaultVimNormalBg = "#BCBCBC";
    static const QString defaultVimInsertBg = "#C5CAE9";
    static const QString defaultVimVisualBg = "#90CAF9";
    static const QString defaultVimReplaceBg = "#F8BBD0";
    static const QString defaultTrailingSpaceBg = "#A8A8A8";
    static const QString defaultSelectedWordBg = "#DFDF00";
    static const QString defaultSearchedWordBg = "#81C784";
    static const QString defaultSearchedWordCursorBg = "#4DB6AC";
    static const QString defaultIncrementalSearchedWordBg = "#CE93D8";
    static const QString defaultLineNumberBg = "#BDBDBD";
    static const QString defaultLineNumberFg = "#424242";
    static const QString defaultColorColumnBg = "#DD0000";
    static const QString defaultColorColumnFg = "#FFFF00";
    static const QString defaultPreviewImageLineFg = "#9575CD";

    // Read style file .mdhl
    QString file(getEditorStyleUrl());

    QString styleStr = VUtils::readFileFromDisk(file);
    if (styleStr.isEmpty()) {
        return;
    }

    mdEditPalette = baseEditPalette;
    mdEditFont = baseEditFont;

    VStyleParser parser;
    parser.parseMarkdownStyle(styleStr);

    QMap<QString, QMap<QString, QString>> styles;
    parser.fetchMarkdownEditorStyles(mdEditPalette, mdEditFont, styles);

    mdHighlightingStyles = parser.fetchMarkdownStyles(mdEditFont);
    m_codeBlockStyles = parser.fetchCodeBlockStyles(mdEditFont);

    m_editorCurrentLineBg = defaultCurrentLineBackground;
    m_editorVimInsertBg = defaultVimInsertBg;
    m_editorVimNormalBg = defaultVimNormalBg;
    m_editorVimVisualBg = defaultVimVisualBg;
    m_editorVimReplaceBg = defaultVimReplaceBg;
    auto editorCurrentLineIt = styles.find("editor-current-line");
    if (editorCurrentLineIt != styles.end()) {
        auto backgroundIt = editorCurrentLineIt->find("background");
        if (backgroundIt != editorCurrentLineIt->end()) {
            // Do not need to add "#" here, since this is a built-in attribute.
            m_editorCurrentLineBg = *backgroundIt;
        }

        auto vimBgIt = editorCurrentLineIt->find("vim-insert-background");
        if (vimBgIt != editorCurrentLineIt->end()) {
            m_editorVimInsertBg = "#" + *vimBgIt;
        }

        vimBgIt = editorCurrentLineIt->find("vim-normal-background");
        if (vimBgIt != editorCurrentLineIt->end()) {
            m_editorVimNormalBg = "#" + *vimBgIt;
        }

        vimBgIt = editorCurrentLineIt->find("vim-visual-background");
        if (vimBgIt != editorCurrentLineIt->end()) {
            m_editorVimVisualBg = "#" + *vimBgIt;
        }

        vimBgIt = editorCurrentLineIt->find("vim-replace-background");
        if (vimBgIt != editorCurrentLineIt->end()) {
            m_editorVimReplaceBg = "#" + *vimBgIt;
        }
    }

    m_editorTrailingSpaceBg = defaultTrailingSpaceBg;
    m_editorSelectedWordBg = defaultSelectedWordBg;
    m_editorSearchedWordBg = defaultSearchedWordBg;
    m_editorSearchedWordCursorBg = defaultSearchedWordCursorBg;
    m_editorIncrementalSearchedWordBg = defaultIncrementalSearchedWordBg;
    m_editorLineNumberBg = defaultLineNumberBg;
    m_editorLineNumberFg = defaultLineNumberFg;
    m_editorColorColumnBg = defaultColorColumnBg;
    m_editorColorColumnFg = defaultColorColumnFg;
    m_editorPreviewImageLineFg = defaultPreviewImageLineFg;
    auto editorIt = styles.find("editor");
    if (editorIt != styles.end()) {
        auto it = editorIt->find("trailing-space");
        if (it != editorIt->end()) {
            m_editorTrailingSpaceBg = "#" + *it;
        }

        it = editorIt->find("line-number-background");
        if (it != editorIt->end()) {
            m_editorLineNumberBg = "#" + *it;
        }

        it = editorIt->find("line-number-foreground");
        if (it != editorIt->end()) {
            m_editorLineNumberFg = "#" + *it;
        }

        it = editorIt->find("selected-word-background");
        if (it != editorIt->end()) {
            m_editorSelectedWordBg = "#" + *it;
        }

        it = editorIt->find("searched-word-background");
        if (it != editorIt->end()) {
            m_editorSearchedWordBg = "#" + *it;
        }

        it = editorIt->find("searched-word-cursor-background");
        if (it != editorIt->end()) {
            m_editorSearchedWordCursorBg = "#" + *it;
        }

        it = editorIt->find("incremental-searched-word-background");
        if (it != editorIt->end()) {
            m_editorIncrementalSearchedWordBg = "#" + *it;
        }

        it = editorIt->find("color-column-background");
        if (it != editorIt->end()) {
            m_editorColorColumnBg = "#" + *it;
        }

        it = editorIt->find("color-column-foreground");
        if (it != editorIt->end()) {
            m_editorColorColumnFg = "#" + *it;
        }

        it = editorIt->find("preview-image-line-foreground");
        if (it != editorIt->end()) {
            m_editorPreviewImageLineFg = "#" + *it;
        }
    }
}

void VConfigManager::updateEditStyle()
{
    // Reset font and palette.
    baseEditFont = mdEditFont = m_defaultEditFont;
    baseEditPalette = mdEditPalette = m_defaultEditPalette;

    static const QColor defaultColor = m_defaultEditPalette.color(QPalette::Base);
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

    // Base editor will use the same font size as the markdown editor by now.
    if (mdEditFont.pointSize() > -1) {
        baseEditFont.setPointSize(mdEditFont.pointSize());
    }

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

QString VConfigManager::getConfigFilePath() const
{
    V_ASSERT(userSettings);

    return userSettings->fileName();
}

QString VConfigManager::getStyleConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_styleConfigFolder);
    return path;
}

QString VConfigManager::getCodeBlockStyleConfigFolder() const
{
    static QString path = QDir(getStyleConfigFolder()).filePath(c_codeBlockStyleConfigFolder);
    return path;
}

QString VConfigManager::getTemplateConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_templateConfigFolder);
    return path;
}

QString VConfigManager::getSnippetConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_snippetConfigFolder);
    return path;
}

QString VConfigManager::getSnippetConfigFilePath() const
{
    static QString path = QDir(getSnippetConfigFolder()).filePath(c_snippetConfigFile);
    return path;
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

QVector<QString> VConfigManager::getNoteTemplates(DocType p_type) const
{
    QVector<QString> res;
    QDir dir(getTemplateConfigFolder());
    if (!dir.exists()) {
        dir.mkpath(getTemplateConfigFolder());
        return res;
    }

    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    QStringList files = dir.entryList();
    res.reserve(files.size());
    for (auto const &item : files) {
        if (p_type == DocType::Unknown
            || p_type == VUtils::docTypeFromName(item)) {
            res.push_back(item);
        }
    }

    return res;
}

QVector<QString> VConfigManager::getCodeBlockCssStyles() const
{
    QVector<QString> res;
    QDir dir(getCodeBlockStyleConfigFolder());
    if (!dir.exists()) {
        // Output pre-defined CSS styles to this folder.
        outputDefaultCodeBlockCssStyle();
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
    QString folderPath = getStyleConfigFolder();
    QDir dir(folderPath);
    if (!dir.exists()) {
        if (!dir.mkpath(folderPath)) {
            return false;
        }
    }

    QString srcPath = c_defaultCssFile;
    QString destPath = folderPath + QDir::separator() + QFileInfo(srcPath).fileName();

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

bool VConfigManager::outputDefaultCodeBlockCssStyle() const
{
    // Make sure the styles folder exists.
    QString folderPath = getCodeBlockStyleConfigFolder();
    QDir dir(folderPath);
    if (!dir.exists()) {
        if (!dir.mkpath(folderPath)) {
            return false;
        }
    }

    QString srcPath = c_defaultCodeBlockCssFile;
    QString destPath = folderPath + QDir::separator() + QFileInfo(srcPath).fileName();

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

// The URL will be used in the Web page.
QString VConfigManager::getTemplateCodeBlockCssUrl()
{
    if (!m_templateCodeBlockCssUrl.isEmpty()) {
        return m_templateCodeBlockCssUrl;
    }

    QString cssPath = getCodeBlockStyleConfigFolder() +
                      QDir::separator() +
                      m_templateCodeBlockCss + ".css";
    QUrl cssUrl = QUrl::fromLocalFile(cssPath);
    cssPath = cssUrl.toString();
    if (!QFile::exists(cssUrl.toLocalFile())) {
        // Specified css not exists.
        if (m_templateCss == "vnote") {
            bool ret = outputDefaultCodeBlockCssStyle();
            if (!ret) {
                // Use embedded file.
                cssPath = "qrc" + c_defaultCodeBlockCssFile;
            }
        } else {
            setTemplateCodeBlockCss("vnote");
            return getTemplateCodeBlockCssUrl();
        }
    }

    qDebug() << "use template code block css:" << cssPath;
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

QString VConfigManager::getVnoteNotebookFolderPath()
{
    return QDir::home().filePath(c_vnoteNotebookFolderName);
}

QHash<QString, QString> VConfigManager::readShortcutsFromSettings(QSettings *p_settings,
                                                                  const QString &p_group)
{
    QHash<QString, QString> ret;
    p_settings->beginGroup(p_group);
    QStringList keys = p_settings->childKeys();
    for (auto const & key : keys) {
        if (key.isEmpty()) {
            continue;
        }

        QVariant varVal = p_settings->value(key);
        QString sequence = varVal.toString();
        if (varVal.type() == QVariant::StringList) {
            sequence = varVal.toStringList().join(",");
        }

        sequence = sequence.trimmed();
        if (isValidKeySequence(sequence)) {
            ret.insert(key, sequence);
        }
    }

    p_settings->endGroup();

    return ret;
}

bool VConfigManager::isValidKeySequence(const QString &p_seq)
{
    return p_seq.isEmpty()
           || (p_seq.toLower() != "ctrl+q" && !QKeySequence(p_seq).isEmpty());
}

void VConfigManager::readShortcutsFromSettings()
{
    const QString group("shortcuts");

    m_shortcuts.clear();
    m_shortcuts = readShortcutsFromSettings(defaultSettings, group);

    // Update default settings according to user settings.
    QHash<QString, QString> userShortcuts = readShortcutsFromSettings(userSettings,
                                                                      group);
    QSet<QString> matched;
    matched.reserve(m_shortcuts.size());
    for (auto it = userShortcuts.begin(); it != userShortcuts.end(); ++it) {
        auto defaultIt = m_shortcuts.find(it.key());
        if (defaultIt != m_shortcuts.end()) {
            QString sequence = it.value().trimmed();
            if (sequence != defaultIt.value()) {
                if (isValidKeySequence(sequence)) {
                    matched.insert(it.key());
                    *defaultIt = sequence;
                }
            } else {
                matched.insert(it.key());
            }
        }
    }

    if (matched.size() < m_shortcuts.size()) {
        qDebug() << "override user shortcuts settings using default settings";
        writeShortcutsToSettings(userSettings, group, m_shortcuts);
    }
}

void VConfigManager::readCaptainShortcutsFromSettings()
{
    const QString group("captain_mode_shortcuts");

    m_captainShortcuts.clear();
    m_captainShortcuts = readShortcutsFromSettings(defaultSettings, group);

    // Update default settings according to user settings.
    QHash<QString, QString> userShortcuts = readShortcutsFromSettings(userSettings,
                                                                      group);
    QSet<QString> matched;
    matched.reserve(m_captainShortcuts.size());
    for (auto it = userShortcuts.begin(); it != userShortcuts.end(); ++it) {
        auto defaultIt = m_captainShortcuts.find(it.key());
        if (defaultIt != m_captainShortcuts.end()) {
            QString sequence = it.value().trimmed();
            if (sequence != defaultIt.value()) {
                if (isValidKeySequence(sequence)) {
                    matched.insert(it.key());
                    *defaultIt = sequence;
                }
            } else {
                matched.insert(it.key());
            }
        }
    }

    if (matched.size() < m_captainShortcuts.size()) {
        writeShortcutsToSettings(userSettings, group, m_captainShortcuts);
    }

    qDebug() << "captain mode shortcuts:" << m_captainShortcuts;
}

void VConfigManager::writeShortcutsToSettings(QSettings *p_settings,
                                              const QString &p_group,
                                              const QHash<QString, QString> &p_shortcuts)
{
    p_settings->beginGroup(p_group);
    p_settings->remove("");

    for (auto it = p_shortcuts.begin(); it != p_shortcuts.end(); ++it) {
        p_settings->setValue(it.key(), it.value());
    }

    p_settings->endGroup();
}

QString VConfigManager::getShortcutKeySequence(const QString &p_operation) const
{
    auto it = m_shortcuts.find(p_operation);
    if (it == m_shortcuts.end()) {
        return QString();
    }

    return *it;
}

QString VConfigManager::getCaptainShortcutKeySequence(const QString &p_operation) const
{
    auto it = m_captainShortcuts.find(p_operation);
    if (it == m_captainShortcuts.end()) {
        return QString();
    }

    return *it;
}

void VConfigManager::initDocSuffixes()
{
    m_docSuffixes.clear();

    QString mdSuffix = getConfigFromSettings("global",
                                             "markdown_suffix").toString();
    if (mdSuffix.isEmpty()) {
        mdSuffix = getDefaultConfig("global",
                                    "markdown_suffix").toString();
    }

    Q_ASSERT(!mdSuffix.isEmpty());
    QList<QString> md = mdSuffix.toLower().split(':', QString::SkipEmptyParts);
    md.removeDuplicates();
    m_docSuffixes[(int)DocType::Markdown] = md;

    QList<QString> list;
    list << "ls" << "list";
    m_docSuffixes[(int)DocType::List] = list;

    QList<QString> container;
    container << "co" << "container" << "con";
    m_docSuffixes[(int)DocType::Container] = container;

    QList<QString> html;
    html << "html";
    m_docSuffixes[(int)DocType::Html] = html;

    qDebug() << "doc suffixes" << m_docSuffixes;
}

QVector<VFileSessionInfo> VConfigManager::getLastOpenedFiles()
{
    QVector<VFileSessionInfo> files;
    int size = m_sessionSettings->beginReadArray("last_opened_files");
    for (int i = 0; i < size; ++i) {
        m_sessionSettings->setArrayIndex(i);
        files.push_back(VFileSessionInfo::fromSettings(m_sessionSettings));
    }

    m_sessionSettings->endArray();
    qDebug() << "read" << files.size()
             << "items from [last_opened_files] section";

    return files;
}

void VConfigManager::setLastOpenedFiles(const QVector<VFileSessionInfo> &p_files)
{
    const QString section("last_opened_files");

    // Clear it first
    m_sessionSettings->beginGroup(section);
    m_sessionSettings->remove("");
    m_sessionSettings->endGroup();

    m_sessionSettings->beginWriteArray(section);
    for (int i = 0; i < p_files.size(); ++i) {
        m_sessionSettings->setArrayIndex(i);
        const VFileSessionInfo &info = p_files[i];
        info.toSettings(m_sessionSettings);
    }

    m_sessionSettings->endArray();
    qDebug() << "write" << p_files.size()
             << "items in [last_opened_files] section";

}

QVector<VMagicWord> VConfigManager::getCustomMagicWords()
{
    QVector<VMagicWord> words;
    int size = userSettings->beginReadArray("magic_words");
    for (int i = 0; i < size; ++i) {
        userSettings->setArrayIndex(i);

        VMagicWord word;
        word.m_name = userSettings->value("name").toString();
        word.m_definition = userSettings->value("definition").toString();
        words.push_back(word);
    }

    userSettings->endArray();

    return words;
}

QVector<QPair<QString, QString>> VConfigManager::getExternalEditors() const
{
    QVector<QPair<QString, QString>> ret;
    userSettings->beginGroup("external_editors");
    QStringList keys = userSettings->childKeys();
    for (auto const & key : keys) {
        if (key.isEmpty()) {
            continue;
        }

        ret.push_back(QPair<QString, QString>(key, userSettings->value(key).toString()));
    }

    userSettings->endGroup();

    return ret;
}

const QString &VConfigManager::getFlashPage() const
{
    if (m_flashPage.isEmpty()) {
        VConfigManager *var = const_cast<VConfigManager *>(this);

        var->m_flashPage = var->getConfigFromSettings("global",
                                                      "flash_page").toString();
        if (var->m_flashPage.isEmpty()) {
            var->m_flashPage = var->resetDefaultConfig("global", "flash_page").toString();
        }

        if (VUtils::checkFileNameLegal(m_flashPage)) {
            var->m_flashPage = QDir(getConfigFolder()).filePath(m_flashPage);
        }
    }

    if (!QFileInfo::exists(m_flashPage)) {
        VUtils::touchFile(m_flashPage);
    }

    return m_flashPage;
}
