#include "vkeyboardlayoutmanager.h"

#include <QSharedPointer>
#include <QSettings>
#include <QFileInfo>
#include <QDebug>

#include "vconfigmanager.h"

extern VConfigManager *g_config;

VKeyboardLayoutManager *VKeyboardLayoutManager::s_inst = NULL;

VKeyboardLayoutManager *VKeyboardLayoutManager::inst()
{
    if (!s_inst) {
        s_inst = new VKeyboardLayoutManager();
        s_inst->update(g_config);
    }

    return s_inst;
}

static QSharedPointer<QSettings> layoutSettings(const VConfigManager *p_config,
                                                bool p_create = false)
{
    QSharedPointer<QSettings> settings;
    QString file = p_config->getKeyboardLayoutConfigFilePath();
    if (file.isEmpty()) {
        return settings;
    }

    if (!QFileInfo::exists(file) && !p_create) {
        return settings;
    }

    settings.reset(new QSettings(file, QSettings::IniFormat));
    return settings;
}

static void clearLayoutMapping(const QSharedPointer<QSettings> &p_settings,
                               const QString &p_name)
{
    p_settings->beginGroup(p_name);
    p_settings->remove("");
    p_settings->endGroup();
}

static QHash<int, int> readLayoutMappingInternal(const QSharedPointer<QSettings> &p_settings,
                                                 const QString &p_name)
{
    QHash<int, int> mappings;

    p_settings->beginGroup(p_name);
    QStringList keys = p_settings->childKeys();
    for (auto const & key : keys) {
        if (key.isEmpty()) {
            continue;
        }

        bool ok;
        int keyNum = key.toInt(&ok);
        if (!ok) {
            qWarning() << "readLayoutMappingInternal() skip bad key" << key << "in layout" << p_name;
            continue;
        }

        int valNum = p_settings->value(key).toInt();
        mappings.insert(keyNum, valNum);
    }

    p_settings->endGroup();

    return mappings;
}

static bool writeLayoutMapping(const QSharedPointer<QSettings> &p_settings,
                               const QString &p_name,
                               const QHash<int, int> &p_mappings)
{
    clearLayoutMapping(p_settings, p_name);

    p_settings->beginGroup(p_name);
    for (auto it = p_mappings.begin(); it != p_mappings.end(); ++it) {
        p_settings->setValue(QString::number(it.key()), it.value());
    }
    p_settings->endGroup();

    return true;
}

void VKeyboardLayoutManager::update(VConfigManager *p_config)
{
    m_layout.clear();

    m_layout.m_name = p_config->getKeyboardLayout();
    if (m_layout.m_name.isEmpty()) {
        // No mapping.
        return;
    }

    qDebug() << "using keyboard layout mapping" << m_layout.m_name;

    auto settings = layoutSettings(p_config);
    if (settings.isNull()) {
        return;
    }

    m_layout.setMapping(readLayoutMappingInternal(settings, m_layout.m_name));
}

void VKeyboardLayoutManager::update()
{
    inst()->update(g_config);
}

const VKeyboardLayoutManager::Layout &VKeyboardLayoutManager::currentLayout()
{
    return inst()->m_layout;
}

static QStringList readAvailableLayoutMappings(const QSharedPointer<QSettings> &p_settings)
{
    QString fullKey("global/layout_mappings");
    return p_settings->value(fullKey).toStringList();
}

static void writeAvailableLayoutMappings(const QSharedPointer<QSettings> &p_settings,
                                         const QStringList &p_layouts)
{
    QString fullKey("global/layout_mappings");
    return p_settings->setValue(fullKey, p_layouts);
}

QStringList VKeyboardLayoutManager::availableLayouts()
{
    QStringList layouts;
    auto settings = layoutSettings(g_config);
    if (settings.isNull()) {
        return layouts;
    }

    layouts = readAvailableLayoutMappings(settings);
    return layouts;
}

void VKeyboardLayoutManager::setCurrentLayout(const QString &p_name)
{
    auto mgr = inst();
    if (mgr->m_layout.m_name == p_name) {
        return;
    }

    g_config->setKeyboardLayout(p_name);
    mgr->update(g_config);
}

static bool isValidLayoutName(const QString &p_name)
{
    return !p_name.isEmpty() && p_name.toLower() != "global";
}

bool VKeyboardLayoutManager::addLayout(const QString &p_name)
{
    Q_ASSERT(isValidLayoutName(p_name));

    auto settings = layoutSettings(g_config, true);
    if (settings.isNull()) {
        qWarning() << "fail to open keyboard layout QSettings";
        return false;
    }

    QStringList layouts = readAvailableLayoutMappings(settings);
    if (layouts.contains(p_name)) {
        qWarning() << "Keyboard layout mapping" << p_name << "already exists";
        return false;
    }

    layouts.append(p_name);
    writeAvailableLayoutMappings(settings, layouts);

    clearLayoutMapping(settings, p_name);
    return true;
}

bool VKeyboardLayoutManager::removeLayout(const QString &p_name)
{
    Q_ASSERT(isValidLayoutName(p_name));

    auto settings = layoutSettings(g_config, true);
    if (settings.isNull()) {
        qWarning() << "fail to open keyboard layout QSettings";
        return false;
    }

    QStringList layouts = readAvailableLayoutMappings(settings);
    int idx = layouts.indexOf(p_name);
    if (idx == -1) {
        return true;
    }

    layouts.removeAt(idx);
    writeAvailableLayoutMappings(settings, layouts);

    clearLayoutMapping(settings, p_name);
    return true;
}

bool VKeyboardLayoutManager::renameLayout(const QString &p_name, const QString &p_newName)
{
    Q_ASSERT(isValidLayoutName(p_name));
    Q_ASSERT(isValidLayoutName(p_newName));

    auto settings = layoutSettings(g_config, true);
    if (settings.isNull()) {
        qWarning() << "fail to open keyboard layout QSettings";
        return false;
    }

    QStringList layouts = readAvailableLayoutMappings(settings);
    int idx = layouts.indexOf(p_name);
    if (idx == -1) {
        qWarning() << "fail to find keyboard layout mapping" << p_name << "to rename";
        return false;
    }

    if (layouts.indexOf(p_newName) != -1) {
        qWarning() << "keyboard layout mapping" << p_newName << "already exists";
        return false;
    }

    auto content = readLayoutMappingInternal(settings, p_name);
    // Copy the group.
    if (!writeLayoutMapping(settings, p_newName, content)) {
        qWarning() << "fail to write new layout mapping" << p_newName;
        return false;
    }

    clearLayoutMapping(settings, p_name);

    layouts.replace(idx, p_newName);
    writeAvailableLayoutMappings(settings, layouts);

    // Check current layout.
    if (g_config->getKeyboardLayout() == p_name) {
        Q_ASSERT(inst()->m_layout.m_name == p_name);
        g_config->setKeyboardLayout(p_newName);
        inst()->m_layout.m_name = p_newName;
    }

    return true;
}

QHash<int, int> VKeyboardLayoutManager::readLayoutMapping(const QString &p_name)
{
    QHash<int, int> mappings;
    if (p_name.isEmpty()) {
        return mappings;
    }

    auto settings = layoutSettings(g_config);
    if (settings.isNull()) {
        return mappings;
    }

    return readLayoutMappingInternal(settings, p_name);
}

bool VKeyboardLayoutManager::updateLayout(const QString &p_name,
                                          const QHash<int, int> &p_mapping)
{
    Q_ASSERT(isValidLayoutName(p_name));

    auto settings = layoutSettings(g_config, true);
    if (settings.isNull()) {
        qWarning() << "fail to open keyboard layout QSettings";
        return false;
    }

    QStringList layouts = readAvailableLayoutMappings(settings);
    int idx = layouts.indexOf(p_name);
    if (idx == -1) {
        qWarning() << "fail to find keyboard layout mapping" << p_name << "to update";
        return false;
    }

    if (!writeLayoutMapping(settings, p_name, p_mapping)) {
        qWarning() << "fail to write layout mapping" << p_name;
        return false;
    }

    // Check current layout.
    if (inst()->m_layout.m_name == p_name) {
        inst()->m_layout.setMapping(p_mapping);
    }

    return true;
}
