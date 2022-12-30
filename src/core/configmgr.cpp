#include "configmgr.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QResource>
#include <QPixmap>
#include <QSplashScreen>
#include <QScopedPointer>
#include <QTemporaryDir>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include "exception.h"
#include <utils/utils.h>

#include "mainconfig.h"
#include "coreconfig.h"
#include "sessionconfig.h"

using namespace vnotex;

#ifndef QT_NO_DEBUG
// #define VX_DEBUG_WEB
#endif

const QString ConfigMgr::c_orgName = "VNote";

const QString ConfigMgr::c_appName = "VNote";

const QString ConfigMgr::c_configFileName = "vnotex.json";

const QString ConfigMgr::c_sessionFileName = "session.json";

const QString ConfigMgr::c_userFilesFolder = "user_files";

const QString ConfigMgr::c_appFilesFolder = "vnotex_files";

const QJsonObject &ConfigMgr::Settings::getJson() const
{
    return m_jobj;
}

QSharedPointer<ConfigMgr::Settings> ConfigMgr::Settings::fromFile(const QString &p_jsonFilePath)
{
    if (!QFileInfo::exists(p_jsonFilePath)) {
        qWarning() << "return empty Settings from non-exist config file" << p_jsonFilePath;
        return QSharedPointer<Settings>::create();
    }

    auto bytes = FileUtils::readFile(p_jsonFilePath);
    return QSharedPointer<Settings>::create(QJsonDocument::fromJson(bytes).object());
}

void ConfigMgr::Settings::writeToFile(const QString &p_jsonFilePath) const
{
    FileUtils::writeFile(p_jsonFilePath, QJsonDocument(this->m_jobj).toJson());
}

ConfigMgr::ConfigMgr(bool p_isUnitTest, QObject *p_parent)
    : QObject(p_parent),
      m_config(new MainConfig(this)),
      m_sessionConfig(new SessionConfig(this))
{
    if (p_isUnitTest) {
        m_dirForUnitTest.reset(new QTemporaryDir());
        if (!m_dirForUnitTest->isValid()) {
            qWarning() << "failed to init ConfigMgr for UnitTest";
            return;
        }

        QDir dir(m_dirForUnitTest->path());
        dir.mkdir(c_appFilesFolder);
        dir.mkdir(c_userFilesFolder);

        m_appConfigFolderPath = m_dirForUnitTest->filePath(c_appFilesFolder);
        m_userConfigFolderPath = m_dirForUnitTest->filePath(c_userFilesFolder);
        return;
    }

    locateConfigFolder();

    bool needUpdate = checkAppConfig();
    if (needUpdate) {
        checkUserConfig();
    }

    m_config->init();
    m_sessionConfig->init();
}

ConfigMgr::~ConfigMgr()
{
}

ConfigMgr &ConfigMgr::getInst(bool p_isUnitTest)
{
    static ConfigMgr inst(p_isUnitTest);
    return inst;
}

void ConfigMgr::initForUnitTest()
{
    getInst(true);
}

void ConfigMgr::locateConfigFolder()
{
    const auto appDirPath = getApplicationDirPath();
    qInfo() << "app folder" << appDirPath;
    // Check app config.
    {
        QString folderPath(appDirPath + '/' + c_appFilesFolder);
        if (QDir(folderPath).exists()) {
            // Config folder in app/.
            m_appConfigFolderPath = PathUtils::cleanPath(folderPath);
        } else {
            m_appConfigFolderPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        }
    }

    // Check user config.
    {
        QString folderPath(appDirPath + '/' + c_userFilesFolder);
        if (QDir(folderPath).exists()) {
            // Config folder in app/.
            m_userConfigFolderPath = PathUtils::cleanPath(folderPath);
        } else {
            m_userConfigFolderPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

            // Make sure it exists.
            QDir dir(m_userConfigFolderPath);
            dir.mkpath(m_userConfigFolderPath);
        }
    }

    Q_ASSERT(m_appConfigFolderPath != m_userConfigFolderPath);
    qInfo() << "app config folder" << m_appConfigFolderPath;
    qInfo() << "user config folder" << m_userConfigFolderPath;
}

bool ConfigMgr::checkAppConfig()
{
    bool needUpdate = false;
    QDir appConfigDir(m_appConfigFolderPath);
    if (!appConfigDir.exists()) {
        needUpdate = true;
        appConfigDir.mkpath(m_appConfigFolderPath);
    } else {
        if (!appConfigDir.exists(c_configFileName)) {
            needUpdate = true;
        } else {
            // Check version of config file.
            auto defaultSettings = getSettings(Source::Default);
            auto appSettings = getSettings(Source::App);
            auto defaultVersion = MainConfig::getVersion(defaultSettings->getJson());
            auto appVersion = MainConfig::getVersion(appSettings->getJson());
            if (defaultVersion != appVersion) {
                qInfo() << "new version" << appVersion << defaultVersion;
                needUpdate = true;
            }
        }

        if (needUpdate) {
            FileUtils::removeDir(m_appConfigFolderPath);
            // Wait for the OS delete the folder.
            Utils::sleepWait(1000);
            appConfigDir.mkpath(m_appConfigFolderPath);
        }
    }

    const auto mainConfigFilePath = appConfigDir.filePath(c_configFileName);

#ifndef VX_DEBUG_WEB
    if (!needUpdate) {
        return false;
    }
#endif

    qInfo() << "update app config files in" << m_appConfigFolderPath;

    Q_ASSERT(appConfigDir.exists());

    QPixmap pixmap(":/vnotex/data/core/logo/vnote.png");
    QScopedPointer<QSplashScreen> splash(new QSplashScreen(pixmap));
    splash->show();

    // Load extra data.
    splash->showMessage("Loading extra resource data");
    const QString extraRcc(PathUtils::concatenateFilePath(QCoreApplication::applicationDirPath(),
                                                          QStringLiteral("vnote_extra.rcc")));
    bool ret = QResource::registerResource(extraRcc);
    if (!ret) {
        Exception::throwOne(Exception::Type::FailToReadFile,
                            QString("failed to register resource file %1").arg(extraRcc));
    }
    auto cleanup = qScopeGuard([extraRcc]() {
        QResource::unregisterResource(extraRcc);
    });

    const QString extraDataRoot(QStringLiteral(":/vnotex/data/extra"));

#ifdef VX_DEBUG_WEB
    if (!needUpdate) {
        // Always update main config file and web folder.
        qDebug() << "forced to update main config file and web folder for debugging";
        splash->showMessage("update main config file and web folder for debugging");

        // Cancel the read-only permission of the main config file.
        QFile::setPermissions(mainConfigFilePath, QFile::WriteUser);
        FileUtils::removeFile(mainConfigFilePath);
        FileUtils::removeDir(appConfigDir.filePath(QStringLiteral("web")));

        // Wait for the OS delete the folder.
        Utils::sleepWait(1000);

        FileUtils::copyFile(getConfigFilePath(Source::Default), mainConfigFilePath);
        FileUtils::copyDir(extraDataRoot + QStringLiteral("/web"),
                           appConfigDir.filePath(QStringLiteral("web")));
        return false;
    }
#else
    Q_ASSERT(needUpdate);
#endif

    // Copy themes.
    qApp->processEvents();
    splash->showMessage("Copying themes");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/themes"),
                       appConfigDir.filePath(QStringLiteral("themes")));

    // Copy tasks.
    qApp->processEvents();
    splash->showMessage("Copying tasks");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/tasks"),
                       appConfigDir.filePath(QStringLiteral("tasks")));

    // Copy docs.
    qApp->processEvents();
    splash->showMessage("Copying docs");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/docs"),
                       appConfigDir.filePath(QStringLiteral("docs")));

    // Copy syntax-highlighting.
    qApp->processEvents();
    splash->showMessage("Copying syntax-highlighting");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/syntax-highlighting"),
                       appConfigDir.filePath(QStringLiteral("syntax-highlighting")));

    // Copy web.
    qApp->processEvents();
    splash->showMessage("Copying web");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/web"),
                       appConfigDir.filePath(QStringLiteral("web")));

    // Copy dicts.
    qApp->processEvents();
    splash->showMessage("Copying dicts");
    FileUtils::copyDir(extraDataRoot + QStringLiteral("/dicts"),
                       appConfigDir.filePath(QStringLiteral("dicts")));

    // Main config file.
    FileUtils::copyFile(getConfigFilePath(Source::Default), appConfigDir.filePath(c_configFileName));

    return needUpdate;
}

void ConfigMgr::checkUserConfig()
{
    // Mainly check if the user config and session config is read-only.
    const auto userFile = getConfigFilePath(Source::User);
    if (QFileInfo::exists(userFile)) {
        if (!(QFile::permissions(userFile) & QFile::WriteUser)) {
            qDebug() << "make user config file writable" << userFile;
            QFile::setPermissions(userFile, QFile::WriteUser);
        }
    }

    const auto sessionFile = getConfigFilePath(Source::Session);
    if (QFileInfo::exists(sessionFile)) {
        if (!(QFile::permissions(sessionFile) & QFile::WriteUser)) {
            qDebug() << "make session config file writable" << sessionFile;
            QFile::setPermissions(sessionFile, QFile::WriteUser);
        }
    }
}

QString ConfigMgr::getConfigFilePath(Source p_src) const
{
    QString configPath;
    switch (p_src) {
    case Source::Default:
        configPath = getDefaultConfigFilePath();
        break;

    case Source::App:
        configPath = m_appConfigFolderPath + QLatin1Char('/') + c_configFileName;
        break;

    case Source::User:
    {
        configPath = m_userConfigFolderPath + QLatin1Char('/') + c_configFileName;
        break;
    }

    case Source::Session:
    {
        configPath = m_userConfigFolderPath + QLatin1Char('/') + c_sessionFileName;
        break;
    }

    default:
        Q_ASSERT(false);
    }

    return configPath;
}

QString ConfigMgr::getDefaultConfigFilePath()
{
    return QStringLiteral(":/vnotex/data/core/") + c_configFileName;
}

QSharedPointer<ConfigMgr::Settings> ConfigMgr::getSettings(Source p_src) const
{
    return ConfigMgr::Settings::fromFile(getConfigFilePath(p_src));
}

void ConfigMgr::writeUserSettings(const QJsonObject &p_jobj)
{
    Settings settings(p_jobj);
    settings.writeToFile(getConfigFilePath(Source::User));
}

void ConfigMgr::writeSessionSettings(const QJsonObject &p_jobj)
{
    Settings settings(p_jobj);
    settings.writeToFile(getConfigFilePath(Source::Session));
}

MainConfig &ConfigMgr::getConfig()
{
    return *m_config;
}

SessionConfig &ConfigMgr::getSessionConfig()
{
    return *m_sessionConfig;
}

CoreConfig &ConfigMgr::getCoreConfig()
{
    return m_config->getCoreConfig();
}

EditorConfig &ConfigMgr::getEditorConfig()
{
    return m_config->getEditorConfig();
}

WidgetConfig &ConfigMgr::getWidgetConfig()
{
    return m_config->getWidgetConfig();
}

QString ConfigMgr::getAppFolder() const
{
    return m_appConfigFolderPath;
}

QString ConfigMgr::getUserFolder() const
{
    return m_userConfigFolderPath;
}

QString ConfigMgr::getAppThemeFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("themes"));
}

QString ConfigMgr::getUserThemeFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("themes"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getAppTaskFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("tasks"));
}

QString ConfigMgr::getUserTaskFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("tasks"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getAppWebStylesFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("web-styles"));
}

QString ConfigMgr::getUserWebStylesFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("web-styles"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getAppDocsFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath, QStringLiteral("docs"));
}

QString ConfigMgr::getUserDocsFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("docs"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getAppSyntaxHighlightingFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath,
                                          QStringLiteral("syntax-highlighting"));
}

QString ConfigMgr::getUserSyntaxHighlightingFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath,
                                                     QStringLiteral("syntax-highlighting"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getAppDictsFolder() const
{
    return PathUtils::concatenateFilePath(m_appConfigFolderPath,
                                          QStringLiteral("dicts"));
}

QString ConfigMgr::getUserDictsFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath,
                                                     QStringLiteral("dicts"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getUserTemplateFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("templates"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getUserSnippetFolder() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("snippets"));
    QDir().mkpath(folderPath);
    return folderPath;
}

QString ConfigMgr::getUserMarkdownUserStyleFile() const
{
    auto folderPath = PathUtils::concatenateFilePath(m_userConfigFolderPath, QStringLiteral("web/css"));
    auto filePath = PathUtils::concatenateFilePath(folderPath, QStringLiteral("user.css"));
    if (!QFileInfo::exists(filePath)) {
        QDir().mkpath(folderPath);
        FileUtils::writeFile(filePath, QByteArray());
    }
    return filePath;
}

QString ConfigMgr::getUserOrAppFile(const QString &p_filePath) const
{
    QFileInfo fi(p_filePath);
    if (fi.isAbsolute()) {
        return p_filePath;
    }

    // Check user folder first.
    QDir userConfigDir(m_userConfigFolderPath);
    if (userConfigDir.exists(p_filePath)) {
        return userConfigDir.absoluteFilePath(p_filePath);
    }

    // App folder.
    QDir appConfigDir(m_appConfigFolderPath);
    return appConfigDir.absoluteFilePath(p_filePath);
}

QString ConfigMgr::locateSessionConfigFilePathAtBootstrap()
{
    // QApplication is not init yet, so org and app name are empty here.
    auto folderPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    folderPath = PathUtils::concatenateFilePath(folderPath, c_orgName + "/" + c_appName);
    QDir dir(folderPath);
    if (dir.exists(c_sessionFileName)) {
        qInfo() << "locateSessionConfigFilePathAtBootstrap" << folderPath;
        return dir.filePath(c_sessionFileName);
    }

    return QString();
}

QString ConfigMgr::getLogFile() const
{
    return PathUtils::concatenateFilePath(ConfigMgr::getInst().getUserFolder(), "vnotex.log");
}

QString ConfigMgr::getApplicationFilePath()
{
#if defined(Q_OS_LINUX)
    // We could get the APPIMAGE env variable from the AppRun script.
    auto appImageVar = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    qInfo() << "APPIMAGE" << appImageVar;
    if (!appImageVar.isEmpty()) {
        return appImageVar;
    }
#elif defined(Q_OS_MACOS)
    auto exePath = QCoreApplication::applicationFilePath();
    const QString exeName = c_appName.toLower() + ".app";
    int idx = exePath.indexOf(exeName + QStringLiteral("/Contents/MacOS/"));
    if (idx != -1) {
        return exePath.left(idx + exeName.size());
    }
#endif

    return QCoreApplication::applicationFilePath();
}

QString ConfigMgr::getApplicationDirPath()
{
    return PathUtils::parentDirPath(getApplicationFilePath());
}

QString ConfigMgr::getDocumentOrHomePath()
{
    static QString docHomePath;
    if (docHomePath.isEmpty()) {
        QStringList folders = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
        if (folders.isEmpty()) {
            docHomePath = QDir::homePath();
        } else {
            docHomePath = folders[0];
        }
    }

    return docHomePath;
}

QString ConfigMgr::getApplicationVersion()
{
    static QString appVersion;

    if (appVersion.isEmpty()) {
        auto defaultSettings = ConfigMgr::Settings::fromFile(getDefaultConfigFilePath());
        const auto &defaultObj = defaultSettings->getJson();

        auto metaDataObj = defaultObj.value(QStringLiteral("metadata")).toObject();
        appVersion = metaDataObj.value(QStringLiteral("version")).toString();
    }

    return appVersion;
}

QJsonValue ConfigMgr::parseAndReadConfig(const QString &p_exp) const
{
    if (p_exp.startsWith(QStringLiteral("main."))) {
        return Utils::parseAndReadJson(m_config->toJson(), p_exp.mid(5));
    } else if (p_exp.startsWith(QStringLiteral("session."))) {
        return Utils::parseAndReadJson(m_sessionConfig->toJson(), p_exp.mid(8));
    } else {
        return QJsonValue();
    }
}
