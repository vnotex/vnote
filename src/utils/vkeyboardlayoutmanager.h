#ifndef VKEYBOARDLAYOUTMANAGER_H
#define VKEYBOARDLAYOUTMANAGER_H

#include <QString>
#include <QStringList>
#include <QHash>

class VConfigManager;

class VKeyboardLayoutManager
{
public:
    struct Layout
    {
        void clear()
        {
            m_name.clear();
            m_mapping.clear();
        }

        void setMapping(const QHash<int, int> &p_mapping)
        {
            m_mapping.clear();

            for (auto it = p_mapping.begin(); it != p_mapping.end(); ++it) {
                m_mapping.insert(it.value(), it.key());
            }
        }

        QString m_name;
        // Reversed mapping.
        QHash<int, int> m_mapping;
    };

    static void update();

    static const VKeyboardLayoutManager::Layout &currentLayout();

    static void setCurrentLayout(const QString &p_name);

    static QStringList availableLayouts();

    static bool addLayout(const QString &p_name);

    static bool removeLayout(const QString &p_name);

    static bool renameLayout(const QString &p_name, const QString &p_newName);

    static bool updateLayout(const QString &p_name, const QHash<int, int> &p_mapping);

    static QHash<int, int> readLayoutMapping(const QString &p_name);

    static int mapKey(int p_key);

private:
    VKeyboardLayoutManager() {}

    static VKeyboardLayoutManager *inst();

    void update(VConfigManager *p_config);

    Layout m_layout;

    static VKeyboardLayoutManager *s_inst;
};

inline int VKeyboardLayoutManager::mapKey(int p_key)
{
    const Layout &layout = inst()->m_layout;
    if (!layout.m_name.isEmpty()) {
        auto it = layout.m_mapping.find(p_key);
        if (it != layout.m_mapping.end()) {
            return it.value();
        }
    }

    return p_key;
}
#endif // VKEYBOARDLAYOUTMANAGER_H
