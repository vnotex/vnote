#include "coreconfig.h"

#include <QMetaEnum>
#include <QLocale>

using namespace vnotex;

#define READSTR(key) readString(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))

QStringList CoreConfig::s_availableLocales;

CoreConfig::CoreConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("core");
}

const QString &CoreConfig::getTheme() const
{
    return m_theme;
}

void CoreConfig::setTheme(const QString &p_name)
{
    updateConfig(m_theme, p_name, this);
}

void CoreConfig::init(const QJsonObject &p_app,
                      const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    m_theme = READSTR(QStringLiteral("theme"));

    m_locale = READSTR(QStringLiteral("locale"));
    if (!m_locale.isEmpty() && !getAvailableLocales().contains(m_locale)) {
        m_locale = QStringLiteral("en_US");
    }

    loadShortcuts(appObj, userObj);

    m_toolBarIconSize = READINT(QStringLiteral("toolbar_icon_size"));
    if (m_toolBarIconSize <= 0) {
        m_toolBarIconSize = 16;
    }
}

QJsonObject CoreConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("theme")] = m_theme;
    obj[QStringLiteral("locale")] = m_locale;
    obj[QStringLiteral("shortcuts")] = saveShortcuts();
    obj[QStringLiteral("toolbar_icon_size")] = m_toolBarIconSize;
    return obj;
}

const QString &CoreConfig::getLocale() const
{
    return m_locale;
}

void CoreConfig::setLocale(const QString &p_locale)
{
    updateConfig(m_locale, p_locale, this);
}

QString CoreConfig::getLocaleToUse() const
{
    return QLocale().name();
}

const QStringList &CoreConfig::getAvailableLocales()
{
    if (s_availableLocales.isEmpty()) {
        s_availableLocales << QStringLiteral("en_US");
        s_availableLocales << QStringLiteral("zh_CN");
    }

    return s_availableLocales;
}

void CoreConfig::loadShortcuts(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(QStringLiteral("shortcuts")).toObject();
    const auto userObj = p_user.value(QStringLiteral("shortcuts")).toObject();

    static const auto indexOfShortcutEnum = CoreConfig::staticMetaObject.indexOfEnumerator("Shortcut");
    Q_ASSERT(indexOfShortcutEnum >= 0);
    const auto metaEnum = CoreConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
    // Skip the Max flag.
    for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
        m_shortcuts[i] = READSTR(metaEnum.key(i));
    }
}

QJsonObject CoreConfig::saveShortcuts() const
{
    QJsonObject obj;
    static const auto indexOfShortcutEnum = CoreConfig::staticMetaObject.indexOfEnumerator("Shortcut");
    Q_ASSERT(indexOfShortcutEnum >= 0);
    const auto metaEnum = CoreConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
    // Skip the Max flag.
    for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
        obj[metaEnum.key(i)] = m_shortcuts[i];
    }
    return obj;
}

const QString &CoreConfig::getShortcut(Shortcut p_shortcut) const
{
    Q_ASSERT(p_shortcut < Shortcut::MaxShortcut);
    return m_shortcuts[p_shortcut];
}

int CoreConfig::getToolBarIconSize() const
{
    return m_toolBarIconSize;
}

void CoreConfig::setToolBarIconSize(int p_size)
{
    Q_ASSERT(p_size > 0);
    updateConfig(m_toolBarIconSize, p_size, this);
}
