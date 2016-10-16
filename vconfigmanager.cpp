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

    // Update notebooks
    readNotebookFromSettings();

    updateMarkdownEditStyle();
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
