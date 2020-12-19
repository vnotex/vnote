#include "editorconfig.h"

#include <QMetaEnum>
#include <QDebug>

#include "texteditorconfig.h"
#include "markdowneditorconfig.h"

using namespace vnotex;

#define READINT(key) readInt(appObj, userObj, (key))
#define READSTR(key) readString(appObj, userObj, (key))

EditorConfig::EditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig),
      m_textEditorConfig(new TextEditorConfig(p_mgr, p_topConfig)),
      m_markdownEditorConfig(new MarkdownEditorConfig(p_mgr, p_topConfig, m_textEditorConfig))
{
    m_sessionName = QStringLiteral("editor");
}

EditorConfig::~EditorConfig()
{
}

void EditorConfig::init(const QJsonObject &p_app,
                        const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    loadCore(appObj, userObj);

    m_textEditorConfig->init(appObj, userObj);
    m_markdownEditorConfig->init(appObj, userObj);
}

void EditorConfig::loadCore(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(QStringLiteral("core")).toObject();
    const auto userObj = p_user.value(QStringLiteral("core")).toObject();

    {
        m_toolBarIconSize = READINT(QStringLiteral("toolbar_icon_size"));
        if (m_toolBarIconSize <= 0) {
            m_toolBarIconSize = 14;
        }
    }

    {
        auto autoSavePolicy = READSTR(QStringLiteral("auto_save_policy"));
        m_autoSavePolicy = stringToAutoSavePolicy(autoSavePolicy);
    }

    m_backupFileDirectory = READSTR(QStringLiteral("backup_file_directory"));

    m_backupFileExtension = READSTR(QStringLiteral("backup_file_extension"));

    loadShortcuts(appObj, userObj);
}

QJsonObject EditorConfig::saveCore() const
{
    QJsonObject obj;
    obj[QStringLiteral("toolbar_icon_size")] = m_toolBarIconSize;
    obj[QStringLiteral("auto_save_policy")] = autoSavePolicyToString(m_autoSavePolicy);
    obj[QStringLiteral("backup_file_directory")] = m_backupFileDirectory;
    obj[QStringLiteral("backup_file_extension")] = m_backupFileExtension;
    obj[QStringLiteral("shortcuts")] = saveShortcuts();
    return obj;
}

void EditorConfig::loadShortcuts(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(QStringLiteral("shortcuts")).toObject();
    const auto userObj = p_user.value(QStringLiteral("shortcuts")).toObject();

    static const auto indexOfShortcutEnum = EditorConfig::staticMetaObject.indexOfEnumerator("Shortcut");
    Q_ASSERT(indexOfShortcutEnum >= 0);
    const auto metaEnum = EditorConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
    // Skip the Max flag.
    for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
        m_shortcuts[i] = READSTR(metaEnum.key(i));
    }
}

QJsonObject EditorConfig::saveShortcuts() const
{
    QJsonObject obj;
    static const auto indexOfShortcutEnum = EditorConfig::staticMetaObject.indexOfEnumerator("Shortcut");
    Q_ASSERT(indexOfShortcutEnum >= 0);
    const auto metaEnum = EditorConfig::staticMetaObject.enumerator(indexOfShortcutEnum);
    // Skip the Max flag.
    for (int i = 0; i < metaEnum.keyCount() - 1; ++i) {
        obj[metaEnum.key(i)] = m_shortcuts[i];
    }
    return obj;
}

QJsonObject EditorConfig::toJson() const
{
    QJsonObject obj;
    obj[m_textEditorConfig->getSessionName()] = m_textEditorConfig->toJson();
    obj[m_markdownEditorConfig->getSessionName()] = m_markdownEditorConfig->toJson();
    obj[QStringLiteral("core")] = saveCore();
    return obj;
}

TextEditorConfig &EditorConfig::getTextEditorConfig()
{
    return *m_textEditorConfig;
}

const TextEditorConfig &EditorConfig::getTextEditorConfig() const
{
    return *m_textEditorConfig;
}

MarkdownEditorConfig &EditorConfig::getMarkdownEditorConfig()
{
    return *m_markdownEditorConfig;
}

const MarkdownEditorConfig &EditorConfig::getMarkdownEditorConfig() const
{
    return *m_markdownEditorConfig;
}

int EditorConfig::getToolBarIconSize() const
{
    return m_toolBarIconSize;
}

void EditorConfig::setToolBarIconSize(int p_size)
{
    Q_ASSERT(p_size > 0);
    updateConfig(m_toolBarIconSize, p_size, this);
}

const QString &EditorConfig::getShortcut(Shortcut p_shortcut) const
{
    Q_ASSERT(p_shortcut < Shortcut::MaxShortcut);
    return m_shortcuts[p_shortcut];
}

QString EditorConfig::autoSavePolicyToString(AutoSavePolicy p_policy) const
{
    switch (p_policy) {
    case AutoSavePolicy::None:
        return QStringLiteral("none");

    case AutoSavePolicy::AutoSave:
        return QStringLiteral("autosave");

    default:
        return QStringLiteral("backupfile");
    }
}

EditorConfig::AutoSavePolicy EditorConfig::stringToAutoSavePolicy(const QString &p_str) const
{
    auto policy = p_str.toLower();
    if (policy == QStringLiteral("none")) {
        return AutoSavePolicy::None;
    } else if (policy == QStringLiteral("autosave")) {
        return AutoSavePolicy::AutoSave;
    } else {
        return AutoSavePolicy::BackupFile;
    }
}

EditorConfig::AutoSavePolicy EditorConfig::getAutoSavePolicy() const
{
    return m_autoSavePolicy;
}

void EditorConfig::setAutoSavePolicy(EditorConfig::AutoSavePolicy p_policy)
{
    updateConfig(m_autoSavePolicy, p_policy, this);
}

const QString &EditorConfig::getBackupFileDirectory() const
{
    return m_backupFileDirectory;
}

const QString &EditorConfig::getBackupFileExtension() const
{
    return m_backupFileExtension;
}
