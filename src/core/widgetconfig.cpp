#include "widgetconfig.h"

using namespace vnotex;

#define READINT(key) readInt(appObj, userObj, (key))

WidgetConfig::WidgetConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("widget");
}

void WidgetConfig::init(const QJsonObject &p_app,
                        const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    m_outlineAutoExpandedLevel = READINT(QStringLiteral("outline_auto_expanded_level"));
    if (m_outlineAutoExpandedLevel < 0 || m_outlineAutoExpandedLevel > 6) {
        m_outlineAutoExpandedLevel = 6;
    }

    m_findAndReplaceOptions = static_cast<FindOptions>(READINT(QStringLiteral("find_and_replace_options")));
}

QJsonObject WidgetConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("outline_auto_expanded_level")] = m_outlineAutoExpandedLevel;
    obj[QStringLiteral("find_and_replace_options")] = static_cast<int>(m_findAndReplaceOptions);
    return obj;
}

int WidgetConfig::getOutlineAutoExpandedLevel() const
{
    return m_outlineAutoExpandedLevel;
}

void WidgetConfig::setOutlineAutoExpandedLevel(int p_level)
{
    updateConfig(m_outlineAutoExpandedLevel, p_level, this);
}

FindOptions WidgetConfig::getFindAndReplaceOptions() const
{
    return m_findAndReplaceOptions;
}

void WidgetConfig::setFindAndReplaceOptions(FindOptions p_options)
{
    updateConfig(m_findAndReplaceOptions, p_options, this);
}
