#ifndef VEXPLORERENTRY_H
#define VEXPLORERENTRY_H

#include <QSettings>

namespace ExplorerConfig
{
    static const QString c_directory = "directory";
    static const QString c_imageFolder = "image_folder";
}

class VExplorerEntry
{
public:
    VExplorerEntry()
        : m_isStarred(false)
    {
    }

    VExplorerEntry(const QString &p_directory,
                   const QString &p_imageFolder,
                   bool p_isStarred = false)
        : m_directory(p_directory),
          m_imageFolder(p_imageFolder),
          m_isStarred(p_isStarred)
    {
    }

    static VExplorerEntry fromSettings(const QSettings *p_settings)
    {
        VExplorerEntry entry;
        entry.m_directory = p_settings->value(ExplorerConfig::c_directory).toString();
        entry.m_imageFolder = p_settings->value(ExplorerConfig::c_imageFolder).toString();
        entry.m_isStarred = true;
        return entry;
    }

    void toSettings(QSettings *p_settings) const
    {
        p_settings->setValue(ExplorerConfig::c_directory, m_directory);
        p_settings->setValue(ExplorerConfig::c_imageFolder, m_imageFolder);
    }

    QString m_directory;

    QString m_imageFolder;

    bool m_isStarred;
};

#endif // VEXPLORERENTRY_H
