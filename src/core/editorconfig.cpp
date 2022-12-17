#include "editorconfig.h"

#include <QMetaEnum>
#include <QDebug>

#include "texteditorconfig.h"
#include "markdowneditorconfig.h"
#include "pdfviewerconfig.h"

#include <vtextedit/viconfig.h>

using namespace vnotex;

#define READINT(key) readInt(appObj, userObj, (key))
#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))

bool EditorConfig::ImageHostItem::operator==(const ImageHostItem &p_other) const
{
    return m_type == p_other.m_type
           && m_name == p_other.m_name
           && m_config == p_other.m_config;
}

void EditorConfig::ImageHostItem::fromJson(const QJsonObject &p_jobj)
{
    m_type = p_jobj[QStringLiteral("type")].toInt();
    m_name = p_jobj[QStringLiteral("name")].toString();
    m_config = p_jobj[QStringLiteral("config")].toObject();
}

QJsonObject EditorConfig::ImageHostItem::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("type")] = m_type;
    obj[QStringLiteral("name")] = m_name;
    obj[QStringLiteral("config")] = m_config;
    return obj;
}


EditorConfig::EditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig),
      m_textEditorConfig(new TextEditorConfig(p_mgr, p_topConfig)),
      m_markdownEditorConfig(new MarkdownEditorConfig(p_mgr, p_topConfig, m_textEditorConfig)),
      m_pdfViewerConfig(new PdfViewerConfig(p_mgr, p_topConfig))
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

    loadImageHost(appObj, userObj);

    m_viConfig = QSharedPointer<vte::ViConfig>::create();
    m_viConfig->fromJson(read(appObj, userObj, QStringLiteral("vi")).toObject());

    m_textEditorConfig->init(appObj, userObj);
    m_markdownEditorConfig->init(appObj, userObj);
    m_pdfViewerConfig->init(appObj, userObj);
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

    m_spellCheckAutoDetectLanguageEnabled = READBOOL(QStringLiteral("spell_check_auto_detect_language"));
    m_spellCheckDefaultDictionary = READSTR(QStringLiteral("spell_check_default_dictionary"));
    if (m_spellCheckDefaultDictionary.isEmpty()) {
        m_spellCheckDefaultDictionary = QStringLiteral("en_US");
    }

    {
        auto lineEnding = READSTR(QStringLiteral("line_ending"));
        m_lineEnding = stringToLineEndingPolicy(lineEnding);
    }
}

QJsonObject EditorConfig::saveCore() const
{
    QJsonObject obj;
    obj[QStringLiteral("toolbar_icon_size")] = m_toolBarIconSize;
    obj[QStringLiteral("auto_save_policy")] = autoSavePolicyToString(m_autoSavePolicy);
    obj[QStringLiteral("backup_file_directory")] = m_backupFileDirectory;
    obj[QStringLiteral("backup_file_extension")] = m_backupFileExtension;
    obj[QStringLiteral("shortcuts")] = saveShortcuts();
    obj[QStringLiteral("spell_check_auto_detect_language")] = m_spellCheckAutoDetectLanguageEnabled;
    obj[QStringLiteral("spell_check_default_dictionary")] = m_spellCheckDefaultDictionary;
    obj[QStringLiteral("line_ending")] = lineEndingPolicyToString(m_lineEnding);
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
    obj[m_pdfViewerConfig->getSessionName()] = m_pdfViewerConfig->toJson();
    obj[QStringLiteral("core")] = saveCore();
    obj[QStringLiteral("image_host")] = saveImageHost();

    // In UT, it may be nullptr.
    if (m_viConfig) {
        obj[QStringLiteral("vi")] = m_viConfig->toJson();
    }
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

PdfViewerConfig &EditorConfig::getPdfViewerConfig()
{
    return *m_pdfViewerConfig;
}

const PdfViewerConfig &EditorConfig::getPdfViewerConfig() const
{
    return *m_pdfViewerConfig;
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

LineEndingPolicy EditorConfig::getLineEndingPolicy() const
{
    return m_lineEnding;
}

void EditorConfig::setLineEndingPolicy(LineEndingPolicy p_ending)
{
    updateConfig(m_lineEnding, p_ending, this);
}

const QString &EditorConfig::getBackupFileDirectory() const
{
    return m_backupFileDirectory;
}

const QString &EditorConfig::getBackupFileExtension() const
{
    return m_backupFileExtension;
}

bool EditorConfig::isSpellCheckAutoDetectLanguageEnabled() const
{
    return m_spellCheckAutoDetectLanguageEnabled;
}

const QString &EditorConfig::getSpellCheckDefaultDictionary() const
{
    return m_spellCheckDefaultDictionary;
}

void EditorConfig::setSpellCheckDefaultDictionary(const QString &p_dict)
{
    updateConfig(m_spellCheckDefaultDictionary, p_dict, this);
}

void EditorConfig::loadImageHost(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const auto appObj = p_app.value(QStringLiteral("image_host")).toObject();
    const auto userObj = p_user.value(QStringLiteral("image_host")).toObject();

    {
        auto arr = read(appObj, userObj, QStringLiteral("hosts")).toArray();
        m_imageHosts.resize(arr.size());
        for (int i = 0; i < arr.size(); ++i) {
            m_imageHosts[i].fromJson(arr[i].toObject());
        }
    }

    m_defaultImageHost = READSTR(QStringLiteral("default_image_host"));
    m_clearObsoleteImageAtImageHost = READBOOL(QStringLiteral("clear_obsolete_image"));
}

QJsonObject EditorConfig::saveImageHost() const
{
    QJsonObject obj;

    {
        QJsonArray arr;
        for (const auto &item : m_imageHosts) {
            arr.append(item.toJson());
        }
        obj[QStringLiteral("hosts")] = arr;
    }

    obj[QStringLiteral("default_image_host")] = m_defaultImageHost;
    obj[QStringLiteral("clear_obsolete_image")] = m_clearObsoleteImageAtImageHost;

    return obj;
}

const QVector<EditorConfig::ImageHostItem> &EditorConfig::getImageHosts() const
{
    return m_imageHosts;
}

void EditorConfig::setImageHosts(const QVector<ImageHostItem> &p_hosts)
{
    updateConfig(m_imageHosts, p_hosts, this);
}

const QString &EditorConfig::getDefaultImageHost() const
{
    return m_defaultImageHost;
}

void EditorConfig::setDefaultImageHost(const QString &p_host)
{
    updateConfig(m_defaultImageHost, p_host, this);
}

bool EditorConfig::isClearObsoleteImageAtImageHostEnabled() const
{
    return m_clearObsoleteImageAtImageHost;
}

void EditorConfig::setClearObsoleteImageAtImageHostEnabled(bool p_enabled)
{
    updateConfig(m_clearObsoleteImageAtImageHost, p_enabled, this);
}

const QSharedPointer<vte::ViConfig> &EditorConfig::getViConfig() const
{
    return m_viConfig;
}
