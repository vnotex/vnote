#include "mainconfig.h"

#include <QJsonObject>
#include <QDebug>

#include "configmgr.h"
#include "coreconfig.h"
#include "editorconfig.h"
#include "widgetconfig.h"

using namespace vnotex;

bool MainConfig::s_versionChanged = false;

MainConfig::MainConfig(ConfigMgr *p_mgr)
    : IConfig(p_mgr, nullptr),
      m_coreConfig(new CoreConfig(p_mgr, this)),
      m_editorConfig(new EditorConfig(p_mgr, this)),
      m_widgetConfig(new WidgetConfig(p_mgr, this))
{
}

MainConfig::~MainConfig()
{

}

void MainConfig::init()
{
    auto mgr = getMgr();
    auto appSettings = mgr->getSettings(ConfigMgr::Source::App);
    auto userSettings = mgr->getSettings(ConfigMgr::Source::User);
    const auto &appJobj = appSettings->getJson();
    const auto &userJobj = userSettings->getJson();

    loadMetadata(appJobj, userJobj);

    m_coreConfig->init(appJobj, userJobj);

    m_editorConfig->init(appJobj, userJobj);

    m_widgetConfig->init(appJobj, userJobj);

    if (isVersionChanged()) {
        doVersionSpecificOverride();

        // Update user config.
        writeToSettings();
    }
}

void MainConfig::loadMetadata(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(QStringLiteral("metadata")).toObject();
    const auto userObj = p_user.value(QStringLiteral("metadata")).toObject();

    m_version = appObj.value(QStringLiteral("version")).toString();
    m_userVersion = userObj.value(QStringLiteral("version")).toString();
    s_versionChanged = m_version != m_userVersion;
    qDebug() << "version" << m_version << "user version" << m_userVersion;
}

QJsonObject MainConfig::saveMetaData() const
{
    QJsonObject metaObj;
    metaObj[QStringLiteral("version")] = m_version;

    return metaObj;
}

bool MainConfig::isVersionChanged()
{
    return s_versionChanged;
}

CoreConfig &MainConfig::getCoreConfig()
{
    return *m_coreConfig;
}

EditorConfig &MainConfig::getEditorConfig()
{
    return *m_editorConfig;
}

WidgetConfig &MainConfig::getWidgetConfig()
{
    return *m_widgetConfig;
}

void MainConfig::writeToSettings() const
{
    getMgr()->writeUserSettings(toJson());
}

QJsonObject MainConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("metadata")] = saveMetaData();
    obj[m_coreConfig->getSessionName()] = m_coreConfig->toJson();
    obj[m_editorConfig->getSessionName()] = m_editorConfig->toJson();
    obj[m_widgetConfig->getSessionName()] = m_widgetConfig->toJson();
    return obj;
}

const QString &MainConfig::getVersion() const
{
    return m_version;
}

QString MainConfig::getVersion(const QJsonObject &p_jobj)
{
    const auto metadataObj = p_jobj.value(QStringLiteral("metadata")).toObject();
    return metadataObj.value(QStringLiteral("version")).toString();
}

void MainConfig::doVersionSpecificOverride()
{
    // In a new version, we may want to change one value by force.
}
