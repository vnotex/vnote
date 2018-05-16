#ifndef VHISTORYENTRY_H
#define VHISTORYENTRY_H

#include <QDate>
#include <QSettings>

namespace HistoryConfig
{
    static const QString c_file = "file";
    static const QString c_date = "date";
    static const QString c_pinned = "pinned";
    static const QString c_isFolder = "is_folder";
}

class VHistoryEntry
{
public:
    VHistoryEntry()
        : m_isPinned(false),
          m_isFolder(false)
    {
    }

    VHistoryEntry(const QString &p_file,
                  const QDate &p_date,
                  bool p_isPinned = false,
                  bool p_isFolder = false)
        : m_file(p_file), m_isPinned(p_isPinned), m_isFolder(p_isFolder)
    {
        m_date = p_date.toString(Qt::ISODate);
    }

    // Fetch VHistoryEntry from @p_settings.
    static VHistoryEntry fromSettings(const QSettings *p_settings)
    {
        VHistoryEntry entry;
        entry.m_file = p_settings->value(HistoryConfig::c_file).toString();
        entry.m_date = p_settings->value(HistoryConfig::c_date).toString();
        entry.m_isPinned = p_settings->value(HistoryConfig::c_pinned).toBool();
        entry.m_isFolder = p_settings->value(HistoryConfig::c_isFolder).toBool();
        return entry;
    }

    void toSettings(QSettings *p_settings) const
    {
        p_settings->setValue(HistoryConfig::c_file, m_file);
        p_settings->setValue(HistoryConfig::c_date, m_date);
        p_settings->setValue(HistoryConfig::c_pinned, m_isPinned);
        p_settings->setValue(HistoryConfig::c_isFolder, m_isFolder);
    }

    // File path.
    QString m_file;

    // Accessed date.
    // UTC in Qt::ISODate format.
    QString m_date;

    bool m_isPinned;

    bool m_isFolder;
};

#endif // VHISTORYENTRY_H
