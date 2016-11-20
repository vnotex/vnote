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

    baseEditFont.setPointSize(11);
    baseEditPalette = QTextEdit().palette();

    welcomePagePath = getConfigFromSettings("global", "welcome_page_path").toString();
    templatePath = getConfigFromSettings("global", "template_path").toString();
    preTemplatePath = getConfigFromSettings("global", "pre_template_path").toString();
    postTemplatePath = getConfigFromSettings("global", "post_template_path").toString();
    templateCssUrl = getConfigFromSettings("global", "template_css_url").toString();
    curNotebookIndex = getConfigFromSettings("global", "current_notebook").toInt();

    markdownExtensions = hoedown_extensions(HOEDOWN_EXT_TABLES | HOEDOWN_EXT_FENCED_CODE |
                                            HOEDOWN_EXT_HIGHLIGHT | HOEDOWN_EXT_AUTOLINK |
                                            HOEDOWN_EXT_QUOTE | HOEDOWN_EXT_MATH);
    mdConverterType = (MarkdownConverterType)getConfigFromSettings("global", "markdown_converter").toInt();

    tabStopWidth = getConfigFromSettings("global", "tab_stop_width").toInt();
    isExpandTab = getConfigFromSettings("global", "is_expand_tab").toBool();

    readPredefinedColorsFromSettings();
    curBackgroundColor = getConfigFromSettings("global", "current_background_color").toString();

    updatePaletteColor();

    curRenderBackgroundColor = getConfigFromSettings("global",
                                                     "current_render_background_color").toString();

    m_toolsDockChecked = getConfigFromSettings("session", "tools_dock_checked").toBool();
    m_mainWindowGeometry = getConfigFromSettings("session", "main_window_geometry").toByteArray();
    m_mainWindowState = getConfigFromSettings("session", "main_window_state").toByteArray();

    // Update notebooks
    readNotebookFromSettings();

    updateMarkdownEditStyle();
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

void VConfigManager::readNotebookFromSettings()
{
    notebooks.clear();
    int size = userSettings->beginReadArray("notebooks");
    for (int i = 0; i < size; ++i) {
        userSettings->setArrayIndex(i);
        VNotebook notebook;
        QString name = userSettings->value("name").toString();
        QString path = userSettings->value("path").toString();
        notebook.setName(name);
        notebook.setPath(path);
        notebooks.append(notebook);
    }
    userSettings->endArray();
    qDebug() << "read" << notebooks.size()
             << "notebook items from [notebooks] section";
}

void VConfigManager::writeNotebookToSettings()
{
    // Clear it first
    userSettings->beginGroup("notebooks");
    userSettings->remove("");
    userSettings->endGroup();

    userSettings->beginWriteArray("notebooks");
    for (int i = 0; i < notebooks.size(); ++i) {
        userSettings->setArrayIndex(i);
        userSettings->setValue("name", notebooks[i].getName());
        userSettings->setValue("path", notebooks[i].getPath());
    }
    userSettings->endArray();
    qDebug() << "write" << notebooks.size()
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
        qWarning() << "error: fail to read directory configuration file:"
                   << configFile;
        return QJsonObject();
    }

    QByteArray configData = config.readAll();
    return QJsonDocument::fromJson(configData).object();
}

bool VConfigManager::writeDirectoryConfig(const QString &path, const QJsonObject &configJson)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "write config file:" << configFile;
    QFile config(configFile);
    if (!config.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to open directory configuration file for write:"
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
        qWarning() << "error: fail to delete directory configuration file:"
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
    QString rgb;
    if (curBackgroundColor == "System") {
        return;
    } else {
        for (int i = 0; i < predefinedColors.size(); ++i) {
            if (predefinedColors[i].name == curBackgroundColor) {
                rgb = predefinedColors[i].rgb;
                break;
            }
        }
    }
    if (rgb.isEmpty()) {
        return;
    }

    baseEditPalette.setColor(QPalette::Base,
                             QColor(VUtils::QRgbFromString(rgb)));

    // Update markdown editor palette
    updateMarkdownEditStyle();
}
