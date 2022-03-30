#ifndef UNITEDENTRYHELPER_H
#define UNITEDENTRYHELPER_H

#include <QString>

#include <core/location.h>

class QIcon;

namespace vnotex
{
    class UnitedEntryHelper
    {
    public:
        struct UserEntry
        {
            QString m_name;

            QString m_args;
        };

        enum ItemType
        {
            Buffer,
            File,
            Folder,
            Notebook,
            Other,
            MaxItemType
        };

        UnitedEntryHelper() = delete;

        static UserEntry parseUserEntry(const QString &p_text);

        static const QIcon &itemIcon(ItemType p_type);

        static ItemType locationTypeToItemType(LocationType p_type);
    };

}

#endif // UNITEDENTRYHELPER_H
