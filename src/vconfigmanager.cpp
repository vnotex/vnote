#include "vconfigmanager.h"
#include <QDir>
#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtDebug>
#include <QTextEdit>
#include "utils/vutils.h"
#include "vstyleparser.h"

const QString VConfigManager::orgName = QString("tamlok");
const QString VConfigManager::appName = QString("vnote");
const QString VConfigManager::dirConfigFileName = QString(".vnote.json");
const QString VConfigManager::defaultConfigFilePath = QString(":/resources/vnote.ini");

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

void VConfigManager::initialize()
{
    userSettings = new QSettings(QSettings::IniFormat, QSettings::UserScope, orgName, appName);
    defaultSettings = new QSettings(defaultConfigFilePath, QSettings::IniFormat);

    m_editorFontSize = getConfigFromSettings("global", "editor_font_size").toInt();
    if (m_editorFontSize <= 0) {
        m_editorFontSize = 12;
    }
    baseEditFont.setPointSize(m_editorFontSize);
    baseEditPalette = QTextEdit().palette();

    welcomePagePath = getConfigFromSettings("global", "welcome_page_path").toString();
    templateCssUrl = getConfigFromSettings("global", "template_css_url").toString();
    curNotebookIndex = getConfigFromSettings("global", "current_notebook").toInt();

    markdownExtensions = hoedown_extensions(HOEDOWN_EXT_TABLES | HOEDOWN_EXT_FENCED_CODE |
                                            HOEDOWN_EXT_HIGHLIGHT | HOEDOWN_EXT_AUTOLINK |
                                            HOEDOWN_EXT_QUOTE | HOEDOWN_EXT_MATH);
    mdConverterType = (MarkdownConverterType)getConfigFromSettings("global", "markdown_converter").toInt();

    tabStopWidth = getConfigFromSettings("global", "tab_stop_width").toInt();
    isExpandTab = getConfigFromSettings("global", "is_expand_tab").toBool();
    m_highlightCursorLine = getConfigFromSettings("global", "highlight_cursor_line").toBool();
    m_highlightSelectedWord = getConfigFromSettings("global", "highlight_selected_word").toBool();
    m_highlightSearchedWord = getConfigFromSettings("global", "highlight_searched_word").toBool();

    readPredefinedColorsFromSettings();
    curBackgroundColor = getConfigFromSettings("global", "current_background_color").toString();

    updatePaletteColor();

    curRenderBackgroundColor = getConfigFromSettings("global",
                                                     "current_render_background_color").toString();

    m_toolsDockChecked = getConfigFromSettings("session", "tools_dock_checked").toBool();
    m_mainWindowGeometry = getConfigFromSettings("session", "main_window_geometry").toByteArray();
    m_mainWindowState = getConfigFromSettings("session", "main_window_state").toByteArray();
    m_mainSplitterState = getConfigFromSettings("session", "main_splitter_state").toByteArray();

    updateMarkdownEditStyle();

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

QVariant VConfigManager::getConfigFromSettings(const QString &section, const QString &key)
{
    QString fullKey = section + "/" + key;
    // First, look up the user-scoped config file
    QVariant value = userSettings->value(fullKey);
    if (!value.isNull()) {
        qDebug() << "user config:" << fullKey << value.toString();
        return value;
    }

    // Second, look up the default config file
    value = defaultSettings->value(fullKey);
    qDebug() << "default config:" << fullKey << value.toString();
    return value;
}

void VConfigManager::setConfigToSettings(const QString &section, const QString &key, const QVariant &value)
{
    // Set the user-scoped config file
    QString fullKey = section + "/" + key;
    userSettings->setValue(fullKey, value);
    qDebug() << "set user config:" << fullKey << value.toString();
}

QJsonObject VConfigManager::readDirectoryConfig(const QString &path)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "read config file:" << configFile;
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
     QString configFile = QDir(path).filePath(dirConfigFileName);
     QFile config(configFile);
     return config.exists();
}

bool VConfigManager::writeDirectoryConfig(const QString &path, const QJsonObject &configJson)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "write config file:" << configFile;
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
    QString configFile = QDir(path).filePath(dirConfigFileName);

    QFile config(configFile);
    if (!config.remove()) {
        qWarning() << "fail to delete directory configuration file:"
                   << configFile;
        return false;
    }
    qDebug() << "delete config file:" << configFile;
    return true;
}

void VConfigManager::updateMarkdownEditStyle()
{
    // Read style file .mdhl
    QString file(":/resources/styles/default.mdhl");

    QString styleStr = VUtils::readFileFromDisk(file);
    if (styleStr.isEmpty()) {
        return;
    }

    VStyleParser parser;
    parser.parseMarkdownStyle(styleStr);
    mdHighlightingStyles = parser.fetchMarkdownStyles(baseEditFont);
    mdEditPalette = baseEditPalette;
    mdEditFont = baseEditFont;
    parser.fetchMarkdownEditorStyles(mdEditPalette, mdEditFont);
}

void VConfigManager::updatePaletteColor()
{
    static const QColor defaultColor = baseEditPalette.color(QPalette::Base);
    QColor newColor = defaultColor;
    if (curBackgroundColor != "System") {
        for (int i = 0; i < predefinedColors.size(); ++i) {
            if (predefinedColors[i].name == curBackgroundColor) {
                QString rgb = predefinedColors[i].rgb;
                if (!rgb.isEmpty()) {
                    newColor = QColor(VUtils::QRgbFromString(rgb));
                }
                break;
            }
        }
    }

    baseEditPalette.setColor(QPalette::Base, newColor);

    // Update markdown editor palette
    updateMarkdownEditStyle();
}
