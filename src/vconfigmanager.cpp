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
#include <QScopedPointer>
#include <QDateTime>

#include "utils/vutils.h"
#include "vstyleparser.h"
#include "vpalette.h"

const QString VConfigManager::orgName = QString("vnote");

const QString VConfigManager::appName = QString("vnote");

const QString VConfigManager::c_version = QString("2.8.2");

const QString VConfigManager::c_dirConfigFile = QString("_vnote.json");

const QString VConfigManager::c_defaultConfigFilePath = QString(":/resources/vnote.ini");

const QString VConfigManager::c_defaultConfigFile = QString("vnote.ini");

const QString VConfigManager::c_sessionConfigFile = QString("session.ini");

const QString VConfigManager::c_snippetConfigFile = QString("snippet.json");

const QString VConfigManager::c_keyboardLayoutConfigFile = QString("keyboard_layouts.ini");

const QString VConfigManager::c_styleConfigFolder = QString("styles");

const QString VConfigManager::c_themeConfigFolder = QString("themes");

const QString VConfigManager::c_codeBlockStyleConfigFolder = QString("codeblock_styles");

const QString VConfigManager::c_templateConfigFolder = QString("templates");

const QString VConfigManager::c_snippetConfigFolder = QString("snippets");

const QString VConfigManager::c_resourceConfigFolder = QString("resources");

const QString VConfigManager::c_warningTextStyle = QString("color: #C9302C; font: bold");

const QString VConfigManager::c_dataTextStyle = QString("font: bold");

const QString VConfigManager::c_vnoteNotebookFolderName = QString("vnote_notebooks");

const QString VConfigManager::c_exportFolderName = QString("vnote_exports");

VConfigManager::VConfigManager(QObject *p_parent)
    : QObject(p_parent),
      m_noteListViewOrder(-1),
      m_explorerCurrentIndex(-1),
      m_hasReset(false),
      userSettings(NULL),
      defaultSettings(NULL),
      m_sessionSettings(NULL)
{
}

void VConfigManager::initialize()
{
    initSettings();

    checkVersion();

    initThemes();

    initEditorStyles();

    initCssStyles();

    initCodeBlockCssStyles();

    m_githubPersonalAccessToken = getConfigFromSettings("global", "github_personal_access_token").toString();
    m_githubReposName = getConfigFromSettings("global", "github_repos_name").toString();
    m_githubUserName = getConfigFromSettings("global", "github_user_name").toString();
    m_githubKeepImgScale = getConfigFromSettings("global", "github_keep_img_scale").toBool();
    m_githubDoNotReplaceLink = getConfigFromSettings("global", "github_do_not_replace_link").toBool();

    m_giteePersonalAccessToken = getConfigFromSettings("global", "gitee_personal_access_token").toString();
    m_giteeReposName = getConfigFromSettings("global", "gitee_repos_name").toString();
    m_giteeUserName = getConfigFromSettings("global", "gitee_user_name").toString();
    m_giteeKeepImgScale = getConfigFromSettings("global", "gitee_keep_img_scale").toBool();
    m_giteeDoNotReplaceLink = getConfigFromSettings("global", "gitee_do_not_replace_link").toBool();

    m_wechatAppid = getConfigFromSettings("global", "wechat_appid").toString();
    m_wechatSecret = getConfigFromSettings("global", "wechat_secret").toString();
    m_markdown2WechatToolUrl = getConfigFromSettings("global", "wechat_markdown_to_wechat_tool_url").toString();
    m_wechatKeepImgScale = getConfigFromSettings("global", "wechat_keep_img_scale").toBool();
    m_wechatDoNotReplaceLink = getConfigFromSettings("global", "wechat_do_not_replace_link").toBool();

    m_tencentAccessDomainName = getConfigFromSettings("global", "tencent_access_domain_name").toString();
    m_tencentSecretId = getConfigFromSettings("global", "tencent_secret_id").toString();
    m_tencentSecretKey = getConfigFromSettings("global", "tencent_secret_key").toString();
    m_tencentKeepImgScale = getConfigFromSettings("global", "tencent_keep_img_scale").toBool();
    m_tencentDoNotReplaceLink = getConfigFromSettings("global", "tencent_do_not_replace_link").toBool();

    m_theme = getConfigFromSettings("global", "theme").toString();

    m_editorStyle = getConfigFromSettings("global", "editor_style").toString();

    m_cssStyle = getConfigFromSettings("global", "css_style").toString();

    m_codeBlockCssStyle = getConfigFromSettings("global", "code_block_css_style").toString();

    m_defaultEditPalette = QTextEdit().palette();

    markdownExtensions = hoedown_extensions(HOEDOWN_EXT_TABLES | HOEDOWN_EXT_FENCED_CODE |
                                            HOEDOWN_EXT_HIGHLIGHT | HOEDOWN_EXT_AUTOLINK |
                                            HOEDOWN_EXT_QUOTE | HOEDOWN_EXT_MATH | HOEDOWN_EXT_MATH_EXPLICIT);
    mdConverterType = (MarkdownConverterType)getConfigFromSettings("global", "markdown_converter").toInt();

    tabStopWidth = getConfigFromSettings("global", "tab_stop_width").toInt();
    isExpandTab = getConfigFromSettings("global", "is_expand_tab").toBool();
    m_highlightCursorLine = getConfigFromSettings("global", "highlight_cursor_line").toBool();
    m_highlightSelectedWord = getConfigFromSettings("global", "highlight_selected_word").toBool();
    m_highlightSearchedWord = getConfigFromSettings("global", "highlight_searched_word").toBool();

    readCustomColors();

    curBackgroundColor = getConfigFromSettings("global", "current_background_color").toString();

    updateEditStyle();

    curRenderBackgroundColor = getConfigFromSettings("global",
                                                     "current_render_background_color").toString();

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

    m_editorZoomDelta = getConfigFromSettings("global", "editor_zoom_delta").toInt();

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

    m_markdownItOpt = MarkdownitOption::fromConfig(getConfigFromSettings("web",
                                                                         "markdownit_opt").toStringList());

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

    m_middleClickCloseTab = getConfigFromSettings("global",
                                                  "middle_click_close_tab").toBool();

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

    m_vimExemptionKeys = getConfigFromSettings("global",
                                               "vim_exemption_keys").toString();

    m_closeBeforeExternalEditor = getConfigFromSettings("global",
                                                        "close_before_external_editor").toBool();

    m_stylesToInlineWhenCopied = getConfigFromSettings("web",
                                                       "styles_to_inline_when_copied").toStringList().join(",");

    m_singleClickClosePreviousTab = getConfigFromSettings("global",
                                                          "single_click_close_previous_tab").toBool();

    m_enableFlashAnchor = getConfigFromSettings("web",
                                                "enable_flash_anchor").toBool();

    m_plantUMLMode = getConfigFromSettings("global", "plantuml_mode").toInt();
    m_plantUMLServer = getConfigFromSettings("web", "plantuml_server").toString();
    m_plantUMLJar = getConfigFromSettings("web", "plantuml_jar").toString();

    QString plantUMLArgs = getConfigFromSettings("web", "plantuml_args").toString();
    m_plantUMLArgs = VUtils::parseCombinedArgString(plantUMLArgs);

    m_plantUMLCmd = getConfigFromSettings("web", "plantuml_cmd").toString();

    m_enableGraphviz = getConfigFromSettings("global", "enable_graphviz").toBool();
    m_graphvizDot = getConfigFromSettings("web", "graphviz_dot").toString();

    m_historySize = getConfigFromSettings("global", "history_size").toInt();
    if (m_historySize < 0) {
        m_historySize = 0;
    }

    m_outlineExpandedLevel = getConfigFromSettings("global",
                                                   "outline_expanded_level").toInt();

    m_imageNamePrefix = getConfigFromSettings("global",
                                              "image_name_prefix").toString();

    m_panelViewState = getConfigFromSettings("global",
                                             "panel_view_state").toInt();

    m_maxTagLabelLength = getConfigFromSettings("global",
                                                "max_tag_label_length").toInt();

    m_maxNumOfTagLabels = getConfigFromSettings("global",
                                                "max_num_of_tag_labels").toInt();

    m_smartLivePreview = getConfigFromSettings("global",
                                               "smart_live_preview").toInt();

    m_insertNewNoteInFront = getConfigFromSettings("global",
                                                   "insert_new_note_in_front").toBool();

    m_highlightMatchesInPage = getConfigFromSettings("global",
                                                     "highlight_matches_in_page").toBool();

    m_syncNoteListToCurrentTab = getConfigFromSettings("global",
                                                       "sync_note_list_to_current_tab").toBool();

    initEditorConfigs();

    initMarkdownConfigs();

    m_enableCodeBlockCopyButton = getConfigFromSettings("web",
                                                        "enable_code_block_copy_button").toBool();
}

void VConfigManager::initEditorConfigs()
{
    const QString section("editor");

    m_autoIndent = getConfigFromSettings(section, "auto_indent").toBool();

    m_autoList = getConfigFromSettings(section, "auto_list").toBool();

    m_autoQuote = getConfigFromSettings(section, "auto_quote").toBool();

    int keyMode = getConfigFromSettings(section, "key_mode").toInt();
    if (keyMode < 0 || keyMode >= (int)KeyMode::Invalid) {
        keyMode = 0;
    }
    m_keyMode = (KeyMode)keyMode;

    m_enableSmartImInVimMode = getConfigFromSettings(section,
                                                     "enable_smart_im_in_vim_mode").toBool();

    QString tmpLeader = getConfigFromSettings(section,
                                              "vim_leader_key").toString();
    if (tmpLeader.isEmpty()) {
        m_vimLeaderKey = QChar(' ');
    } else {
        m_vimLeaderKey = tmpLeader[0];
        if (m_vimLeaderKey.isNull()) {
            m_vimLeaderKey = QChar(' ');
        }
    }

    m_enableTabHighlight = getConfigFromSettings(section,
                                                 "enable_tab_highlight").toBool();

    m_parsePasteLocalImage = getConfigFromSettings(section, "parse_paste_local_image").toBool();

    m_enableExtraBuffer = getConfigFromSettings(section, "enable_extra_buffer").toBool();

    m_autoScrollCursorLine = getConfigFromSettings(section, "auto_scroll_cursor_line").toInt();

    m_editorFontFamily = getConfigFromSettings(section, "editor_font_family").toString();

    m_enableSmartTable = getConfigFromSettings(section, "enable_smart_table").toBool();

    m_tableFormatIntervalMS = getConfigFromSettings(section, "table_format_interval").toInt();
}

void VConfigManager::initMarkdownConfigs()
{
    const QString section("markdown");
    m_enableWavedrom = getConfigFromSettings(section, "enable_wavedrom").toBool();

    m_prependDotInRelativePath = getConfigFromSettings(section, "prepend_dot_in_relative_path").toBool();
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
}

void VConfigManager::readCustomColors()
{
    m_customColors.clear();
    QStringList str = getConfigFromSettings("global", "custom_colors").toStringList();

    for (auto const & item : str) {
        QStringList parts = item.split(':', QString::SkipEmptyParts);
        if (parts.size() != 2) {
            continue;
        }

        if (!QColor(parts[1]).isValid()) {
            continue;
        }

        VColor color;
        color.m_name = parts[0];
        color.m_color = parts[1];
        m_customColors.append(color);
    }
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
        p_settings->setValue("path", notebook.getPathInConfig());
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
    if (m_hasReset) {
        return;
    }

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
    if (m_hasReset) {
        return;
    }

    setConfigToSettingsBySectionKey(m_sessionSettings,
                                    p_section,
                                    p_key,
                                    p_value);
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

QString VConfigManager::getLogFilePath() const
{
    return QDir(getConfigFolder()).filePath("vnote.log");
}

void VConfigManager::updateMarkdownEditStyle()
{
    static const QString defaultColor = "#00897B";

    // Read style file .mdhl
    QString file(getEditorStyleFile());

    qDebug() << "use editor style file" << file;

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

    m_editorCurrentLineBg = defaultColor;
    m_editorVimInsertBg = defaultColor;
    m_editorVimNormalBg = defaultColor;
    m_editorVimVisualBg = defaultColor;
    m_editorVimReplaceBg = defaultColor;

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

    m_editorTrailingSpaceBg = defaultColor;
    m_editorSelectedWordFg = defaultColor;
    m_editorSelectedWordBg = defaultColor;
    m_editorSearchedWordFg = defaultColor;
    m_editorSearchedWordBg = defaultColor;
    m_editorSearchedWordCursorFg = defaultColor;
    m_editorSearchedWordCursorBg = defaultColor;
    m_editorIncrementalSearchedWordFg = defaultColor;
    m_editorIncrementalSearchedWordBg = defaultColor;
    m_editorLineNumberBg = defaultColor;
    m_editorLineNumberFg = defaultColor;
    m_editorColorColumnBg = defaultColor;
    m_editorColorColumnFg = defaultColor;
    m_editorPreviewImageLineFg = defaultColor;
    m_editorPreviewImageBg.clear();

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

        it = editorIt->find("selected-word-foreground");
        if (it != editorIt->end()) {
            m_editorSelectedWordFg = "#" + *it;
        }

        it = editorIt->find("selected-word-background");
        if (it != editorIt->end()) {
            m_editorSelectedWordBg = "#" + *it;
        }

        it = editorIt->find("searched-word-foreground");
        if (it != editorIt->end()) {
            m_editorSearchedWordFg = "#" + *it;
        }

        it = editorIt->find("searched-word-background");
        if (it != editorIt->end()) {
            m_editorSearchedWordBg = "#" + *it;
        }

        it = editorIt->find("searched-word-cursor-foreground");
        if (it != editorIt->end()) {
            m_editorSearchedWordCursorFg = "#" + *it;
        }

        it = editorIt->find("searched-word-cursor-background");
        if (it != editorIt->end()) {
            m_editorSearchedWordCursorBg = "#" + *it;
        }

        it = editorIt->find("incremental-searched-word-foreground");
        if (it != editorIt->end()) {
            m_editorIncrementalSearchedWordFg = "#" + *it;
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

        it = editorIt->find("preview-image-background");
        if (it != editorIt->end()) {
            m_editorPreviewImageBg = "#" + *it;
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
        for (int i = 0; i < m_customColors.size(); ++i) {
            if (m_customColors[i].m_name == curBackgroundColor) {
                newColor = QColor(m_customColors[i].m_color);
                force = true;
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

const QString &VConfigManager::getStyleConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_styleConfigFolder);
    return path;
}

const QString &VConfigManager::getThemeConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_themeConfigFolder);
    return path;
}

const QString &VConfigManager::getCodeBlockStyleConfigFolder() const
{
    static QString path = QDir(getStyleConfigFolder()).filePath(c_codeBlockStyleConfigFolder);
    return path;
}

const QString &VConfigManager::getTemplateConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_templateConfigFolder);
    return path;
}

const QString &VConfigManager::getSnippetConfigFolder() const
{
    static QString path = QDir(getConfigFolder()).filePath(c_snippetConfigFolder);
    return path;
}

const QString &VConfigManager::getSnippetConfigFilePath() const
{
    static QString path = QDir(getSnippetConfigFolder()).filePath(c_snippetConfigFile);
    return path;
}

QString VConfigManager::getResourceConfigFolder() const
{
    return QDir(getConfigFolder()).filePath(c_resourceConfigFolder);
}

const QString &VConfigManager::getCommonCssUrl() const
{
    static QString cssPath;
    if (cssPath.isEmpty()) {
        cssPath = QDir(getResourceConfigFolder()).filePath("common.css");
        if (m_versionChanged || !QFileInfo::exists(cssPath)) {
            VUtils::deleteFile(cssPath);
            // Output the default one.
            if (!VUtils::copyFile(":/resources/common.css", cssPath, false)) {
                cssPath = "qrc:/resources/common.css";
                return cssPath;
            }
        }

        cssPath = QUrl::fromLocalFile(cssPath).toString();
    }

    return cssPath;
}

const QString VConfigManager::getKeyboardLayoutConfigFilePath() const
{
    return QDir(getConfigFolder()).filePath(c_keyboardLayoutConfigFile);
}

QString VConfigManager::getThemeFile() const
{
    auto it = m_themes.find(m_theme);
    if (it != m_themes.end()) {
        return it.value();
    } else {
        qWarning() << "use default theme due to missing specified theme" << m_theme;
        const_cast<VConfigManager *>(this)->m_theme = getDefaultConfig("global", "theme").toString();
        return m_themes[m_theme];
    }
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

// The URL will be used in the Web page.
QString VConfigManager::getCssStyleUrl() const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_cssStyles.isEmpty());

    if (m_cssStyle.isEmpty()) {
        // Use theme's style.
        const_cast<VConfigManager *>(this)->m_cssStyle = VPalette::themeCssStyle(getThemeFile());
    }

    QString cssPath = getCssStyleUrl(m_cssStyle);
    qDebug() << "use css style file" << cssPath;
    return cssPath;
}

QString VConfigManager::getCssStyleUrl(const QString &p_style) const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_cssStyles.isEmpty());

    if (p_style.isEmpty()) {
        return QString();
    }

    QString cssPath;
    auto it = m_cssStyles.find(p_style);
    if (it != m_cssStyles.end()) {
        cssPath = it.value();
    }

    if (cssPath.startsWith(":")) {
        cssPath = "qrc" + cssPath;
    } else {
        QUrl cssUrl = QUrl::fromLocalFile(cssPath);
        cssPath = cssUrl.toString();
    }

    return cssPath;
}

QString VConfigManager::getCodeBlockCssStyleUrl() const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_codeBlockCssStyles.isEmpty());

    if (m_codeBlockCssStyle.isEmpty()) {
        // Use theme's style.
        const_cast<VConfigManager *>(this)->m_codeBlockCssStyle =
            VPalette::themeCodeBlockCssStyle(getThemeFile());
    }

    QString cssPath = getCodeBlockCssStyleUrl(m_codeBlockCssStyle);
    qDebug() << "use code block css style file" << cssPath;
    return cssPath;
}

QString VConfigManager::getCodeBlockCssStyleUrl(const QString &p_style) const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_codeBlockCssStyles.isEmpty());

    if (p_style.isEmpty()) {
        return QString();
    }

    QString cssPath;
    auto it = m_codeBlockCssStyles.find(p_style);
    if (it != m_codeBlockCssStyles.end()) {
        cssPath = it.value();
    }

    if (cssPath.startsWith(":")) {
        cssPath = "qrc" + cssPath;
    } else {
        QUrl cssUrl = QUrl::fromLocalFile(cssPath);
        cssPath = cssUrl.toString();
    }

    return cssPath;
}

QString VConfigManager::getMermaidCssStyleUrl() const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_theme.isEmpty());

    static QString mermaidCssPath;

    if (mermaidCssPath.isEmpty()) {
        VPaletteMetaData data = VPalette::getPaletteMetaData(getThemeFile());
        mermaidCssPath = data.m_mermaidCssFile;
        if (mermaidCssPath.startsWith(":")) {
            mermaidCssPath = "qrc" + mermaidCssPath;
        } else {
            QUrl cssUrl = QUrl::fromLocalFile(mermaidCssPath);
            mermaidCssPath = cssUrl.toString();
        }

        qDebug() << "use mermaid css style file" << mermaidCssPath;
    }

    return mermaidCssPath;
}

QString VConfigManager::getEditorStyleFile() const
{
    Q_ASSERT(!m_themes.isEmpty());
    Q_ASSERT(!m_editorStyles.isEmpty());

    if (m_editorStyle.isEmpty()) {
        // Use theme's style.
        const_cast<VConfigManager *>(this)->m_editorStyle = VPalette::themeEditorStyle(getThemeFile());
    }

    auto it = m_editorStyles.find(m_editorStyle);
    if (it != m_editorStyles.end()) {
        return it.value();
    }

    return QString();
}

QString VConfigManager::getVnoteNotebookFolderPath()
{
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    if (folders.isEmpty()) {
        return QDir::home().filePath(c_vnoteNotebookFolderName);
    } else {
        return QDir(folders[0]).filePath(c_vnoteNotebookFolderName);
    }
}

QString VConfigManager::getExportFolderPath()
{
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    if (folders.isEmpty()) {
        return QDir::home().filePath(c_exportFolderName);
    } else {
        return QDir(folders[0]).filePath(c_exportFolderName);
    }
}

QString VConfigManager::getDocumentPathOrHomePath()
{
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    if (folders.isEmpty()) {
        return QDir::homePath();
    } else {
        return folders[0];
    }
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

    QStringList mdSuffix = getConfigFromSettings("global",
                                                 "markdown_suffix").toStringList();
    if (mdSuffix.isEmpty()) {
        mdSuffix = getDefaultConfig("global",
                                    "markdown_suffix").toStringList();
    }

    for (auto it = mdSuffix.begin(); it != mdSuffix.end();) {
        if (it->isEmpty()) {
            it = mdSuffix.erase(it);
        } else {
            *it = it->toLower();
            ++it;
        }
    }

    Q_ASSERT(!mdSuffix.isEmpty());

    mdSuffix.removeDuplicates();

    m_docSuffixes[(int)DocType::Markdown] = mdSuffix;

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
    if (m_hasReset) {
        return;
    }

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
}

void VConfigManager::getHistory(QLinkedList<VHistoryEntry> &p_history) const
{
    p_history.clear();

    int size = m_sessionSettings->beginReadArray("history");
    for (int i = 0; i < size; ++i) {
        m_sessionSettings->setArrayIndex(i);
        p_history.append(VHistoryEntry::fromSettings(m_sessionSettings));
    }

    m_sessionSettings->endArray();
}

void VConfigManager::setHistory(const QLinkedList<VHistoryEntry> &p_history)
{
    if (m_hasReset) {
        return;
    }

    const QString section("history");

    // Clear it first
    m_sessionSettings->beginGroup(section);
    m_sessionSettings->remove("");
    m_sessionSettings->endGroup();

    m_sessionSettings->beginWriteArray(section);
    int i = 0;
    for (auto it = p_history.begin(); it != p_history.end(); ++it, ++i) {
        m_sessionSettings->setArrayIndex(i);
        it->toSettings(m_sessionSettings);
    }

    m_sessionSettings->endArray();
}

void VConfigManager::getExplorerEntries(QVector<VExplorerEntry> &p_entries) const
{
    p_entries.clear();

    int size = m_sessionSettings->beginReadArray("explorer_starred");
    for (int i = 0; i < size; ++i) {
        m_sessionSettings->setArrayIndex(i);
        p_entries.append(VExplorerEntry::fromSettings(m_sessionSettings));
    }

    m_sessionSettings->endArray();
}

void VConfigManager::setExplorerEntries(const QVector<VExplorerEntry> &p_entries)
{
    if (m_hasReset) {
        return;
    }

    const QString section("explorer_starred");

    // Clear it first
    m_sessionSettings->beginGroup(section);
    m_sessionSettings->remove("");
    m_sessionSettings->endGroup();

    m_sessionSettings->beginWriteArray(section);
    int idx = 0;
    for (auto const & entry : p_entries) {
        if (entry.m_isStarred) {
            m_sessionSettings->setArrayIndex(idx);
            entry.toSettings(m_sessionSettings);
            ++idx;
        }
    }

    m_sessionSettings->endArray();
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

QVector<VExternalEditor> VConfigManager::getExternalEditors() const
{
    QVector<VExternalEditor> ret;
    userSettings->beginGroup("external_editors");
    QStringList keys = userSettings->childKeys();
    for (auto const & key : keys) {
        if (key.isEmpty()) {
            continue;
        }

        QStringList val = userSettings->value(key).toStringList();
        if (val.size() > 2
            || val.isEmpty()) {
            continue;
        }

        VExternalEditor editor;
        editor.m_name = key;
        editor.m_cmd = val[0].trimmed();
        if (editor.m_cmd.isEmpty()) {
            continue;
        }

        if (val.size() == 2) {
            editor.m_shortcut = val[1].trimmed();
        }

        ret.push_back(editor);
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

const QString &VConfigManager::getQuickAccess() const
{
    if (m_quickAccess.isEmpty()) {
        VConfigManager *var = const_cast<VConfigManager *>(this);

        var->m_quickAccess = var->getConfigFromSettings("global",
                                                        "quick_access").toString();
        if (VUtils::checkFileNameLegal(m_quickAccess)) {
            var->m_quickAccess = QDir(getConfigFolder()).filePath(m_quickAccess);
        }
    }

    if (!m_quickAccess.isEmpty() && !QFileInfo::exists(m_quickAccess)) {
        VUtils::touchFile(m_quickAccess);
    }

    return m_quickAccess;
}

void VConfigManager::initThemes()
{
    m_themes.clear();

    // Built-in.
    QString file(":/resources/themes/v_native/v_native.palette");
    m_themes.insert(VPalette::themeName(file), file);
    file = ":/resources/themes/v_pure/v_pure.palette";
    m_themes.insert(VPalette::themeName(file), file);
    file = ":/resources/themes/v_moonlight/v_moonlight.palette";
    m_themes.insert(VPalette::themeName(file), file);
    file = ":/resources/themes/v_detorte/v_detorte.palette";
    m_themes.insert(VPalette::themeName(file), file);
    file = ":/resources/themes/v_simple/v_simple.palette";
    m_themes.insert(VPalette::themeName(file), file);
    file = ":/resources/themes/v_next/v_next.palette";
    m_themes.insert(VPalette::themeName(file), file);

    outputBuiltInThemes();

    // User theme folder.
    QDir dir(getThemeConfigFolder());
    Q_ASSERT(dir.exists());
    if (!dir.exists()) {
        dir.mkpath(getThemeConfigFolder());
        return;
    }

    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    QStringList dirs = dir.entryList();
    for (auto const &item : dirs) {
        QDir themeDir(dir.filePath(item));
        QStringList files = themeDir.entryList(QStringList() << "*.palette");
        if (files.size() != 1) {
            continue;
        }

        QFileInfo fi(files[0]);
        m_themes.insert(VPalette::themeName(files[0]), themeDir.filePath(files[0]));
    }
}

void VConfigManager::outputBuiltInThemes()
{
    QDir dir(getThemeConfigFolder());
    if (!dir.exists()) {
        dir.mkpath(getThemeConfigFolder());
    }

    QStringList suffix({"*.palette"});

    for (auto it = m_themes.begin(); it != m_themes.end(); ++it) {
        QString file = it.value();
        QString srcDir = VUtils::basePathFromPath(file);
        QString folder = VUtils::directoryNameFromPath(srcDir);

        bool needOutput = false;
        if (dir.exists(folder)) {
            QString folderPath = dir.filePath(folder);
            QDir tmpDir(folderPath);
            QStringList files = tmpDir.entryList(suffix);
            if (files.size() == 1) {
                int newVer = VPalette::getPaletteVersion(file);
                int curVer = VPalette::getPaletteVersion(tmpDir.filePath(files[0]));
                if (newVer != curVer) {
                    needOutput = true;
                }
            } else {
                needOutput = true;
            }

            if (needOutput) {
                // Delete the folder.
                bool ret = VUtils::deleteDirectory(folderPath);
                VUtils::sleepWait(100);
                Q_UNUSED(ret);
                qDebug() << "delete obsolete theme" << folderPath << ret;
            }
        } else {
            needOutput = true;
        }

        if (needOutput) {
            qDebug() << "output built-in theme" << file << folder;
            VUtils::copyDirectory(srcDir, dir.filePath(folder), false);
        }
    }
}

void VConfigManager::initEditorStyles()
{
    Q_ASSERT(!m_themes.isEmpty());

    // Styles from themes.
    m_editorStyles = VPalette::editorStylesFromThemes(m_themes.values());

    // User style folder.
    // Get all the .mdhl files in the folder.
    QDir dir(getStyleConfigFolder());
    if (!dir.exists()) {
        dir.mkpath(getStyleConfigFolder());
        return;
    }

    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList("*.mdhl"));
    QStringList files = dir.entryList();
    for (auto const &item : files) {
        QFileInfo fi(item);
        m_editorStyles.insert(fi.completeBaseName(), dir.filePath(item));
    }
}

void VConfigManager::initCssStyles()
{
    Q_ASSERT(!m_themes.isEmpty());

    // Styles from themes.
    m_cssStyles = VPalette::cssStylesFromThemes(m_themes.values());

    // User style folder.
    // Get all the .css files in the folder.
    QDir dir(getStyleConfigFolder());
    if (!dir.exists()) {
        dir.mkpath(getStyleConfigFolder());
        return;
    }

    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList("*.css"));
    QStringList files = dir.entryList();
    for (auto const &item : files) {
        QFileInfo fi(item);
        m_cssStyles.insert(fi.completeBaseName(), dir.filePath(item));
    }
}

void VConfigManager::initCodeBlockCssStyles()
{
    Q_ASSERT(!m_themes.isEmpty());

    // Styles from themes.
    m_codeBlockCssStyles = VPalette::codeBlockCssStylesFromThemes(m_themes.values());

    // User style folder.
    // Get all the .css files in the folder.
    QDir dir(getCodeBlockStyleConfigFolder());
    if (!dir.exists()) {
        dir.mkpath(getCodeBlockStyleConfigFolder());
        return;
    }

    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList("*.css"));
    QStringList files = dir.entryList();
    for (auto const &item : files) {
        QFileInfo fi(item);
        m_codeBlockCssStyles.insert(fi.completeBaseName(), dir.filePath(item));
    }
}

void VConfigManager::resetConfigurations()
{
    // Clear userSettings.
    userSettings->clear();

    // Clear m_sessionSettings except the notebooks information.
    clearGroupOfSettings(m_sessionSettings, "last_opened_files");
    clearGroupOfSettings(m_sessionSettings, "geometry");

    m_hasReset = true;
}

void VConfigManager::resetLayoutConfigurations()
{
    resetDefaultConfig("global", "tools_dock_checked");
    resetDefaultConfig("global", "search_dock_checked");
    resetDefaultConfig("global", "menu_bar_checked");

    clearGroupOfSettings(m_sessionSettings, "geometry");

    m_hasReset = true;
}

void VConfigManager::clearGroupOfSettings(QSettings *p_settings, const QString &p_group)
{
    p_settings->beginGroup(p_group);
    p_settings->remove("");
    p_settings->endGroup();
}

QString VConfigManager::getRenderBackgroundColor(const QString &p_bgName) const
{
    if (p_bgName == "Transparent") {
        return "transparent";
    } else if (p_bgName != "System") {
        for (int i = 0; i < m_customColors.size(); ++i) {
            if (m_customColors[i].m_name == p_bgName) {
                return m_customColors[i].m_color;
            }
        }
    }

    return QString();
}

void VConfigManager::checkVersion()
{
    const QString key("version");
    QString ver = getConfigFromSettings("global", key).toString();
    m_versionChanged = ver != c_version;
    m_freshInstall = ver.isEmpty();
    if (m_versionChanged) {
        setConfigToSettings("global", key, c_version);
    }
}

int VConfigManager::getWindowsOpenGL()
{
    const char *codecForIni = "UTF-8";

    QScopedPointer<QSettings> userSet(new QSettings(QSettings::IniFormat,
                                                    QSettings::UserScope,
                                                    orgName,
                                                    appName));
    userSet->setIniCodec(codecForIni);

    QString fullKey("global/windows_opengl");
    QVariant val = userSet->value(fullKey);
    if (!val.isNull()) {
        return val.toInt();
    }

    // Default vnote.ini from resource file.
    QScopedPointer<QSettings> defaultSet(new QSettings(c_defaultConfigFilePath,
                                                       QSettings::IniFormat));
    defaultSet->setIniCodec(codecForIni);
    return defaultSet->value(fullKey).toInt();
}

void VConfigManager::setWindowsOpenGL(int p_openGL)
{
    const char *codecForIni = "UTF-8";

    QScopedPointer<QSettings> userSet(new QSettings(QSettings::IniFormat,
                                                    QSettings::UserScope,
                                                    orgName,
                                                    appName));
    userSet->setIniCodec(codecForIni);

    QString fullKey("global/windows_opengl");
    userSet->setValue(fullKey, p_openGL);
}

QDate VConfigManager::getLastUserTrackDate() const
{

    auto dateStr = getConfigFromSessionSettings("global",
                                                "last_user_track_date").toString();
    return QDate::fromString(dateStr, Qt::ISODate);
}

void VConfigManager::updateLastUserTrackDate()
{
    auto date = QDate::currentDate();
    setConfigToSessionSettings("global",
                               "last_user_track_date",
                               date.toString(Qt::ISODate));
}

QDateTime VConfigManager::getLastStartDateTime() const
{
    auto dateStr = getConfigFromSessionSettings("global",
                                                "last_start_time").toString();
    return QDateTime::fromString(dateStr, Qt::ISODate);
}

void VConfigManager::updateLastStartDateTime()
{
    auto dateTime = QDateTime::currentDateTime();
    setConfigToSessionSettings("global", "last_start_time", dateTime.toString(Qt::ISODate));
}
