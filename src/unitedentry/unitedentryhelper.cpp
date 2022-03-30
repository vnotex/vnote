#include "unitedentryhelper.h"

#include <QIcon>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <utils/iconutils.h>

using namespace vnotex;

UnitedEntryHelper::UserEntry UnitedEntryHelper::parseUserEntry(const QString &p_text)
{
    UserEntry entry;

    const auto text = p_text.trimmed();

    if (text.isEmpty()) {
        return entry;
    }

    int idx = text.indexOf(QLatin1Char(' '));
    if (idx == -1) {
        entry.m_name = text.toLower();
    } else {
        entry.m_name = text.left(idx).toLower();
        entry.m_args = text.mid(idx).trimmed();
    }

    return entry;
}

const QIcon &UnitedEntryHelper::itemIcon(ItemType p_type)
{
    static QIcon icons[ItemType::MaxItemType];

    if (icons[0].isNull()) {
        // Init.
        const QString nodeIconFgName = "base#icon#fg";
        const auto &themeMgr = VNoteX::getInst().getThemeMgr();
        const auto fg = themeMgr.paletteColor(nodeIconFgName);

        icons[ItemType::Buffer] = IconUtils::fetchIcon(themeMgr.getIconFile("buffer.svg"), fg);
        icons[ItemType::File] = IconUtils::fetchIcon(themeMgr.getIconFile("file_node.svg"), fg);
        icons[ItemType::Folder] = IconUtils::fetchIcon(themeMgr.getIconFile("folder_node.svg"), fg);
        icons[ItemType::Notebook] = IconUtils::fetchIcon(themeMgr.getIconFile("notebook_default.svg"), fg);
        icons[ItemType::Other] = IconUtils::fetchIcon(themeMgr.getIconFile("other_item.svg"), fg);
    }

    return icons[p_type];
}

UnitedEntryHelper::ItemType UnitedEntryHelper::locationTypeToItemType(LocationType p_type)
{
    switch (p_type) {
    case LocationType::Buffer:
        return ItemType::Buffer;

    case LocationType::File:
        return ItemType::File;

    case LocationType::Folder:
        return ItemType::Folder;

    case LocationType::Notebook:
        return ItemType::Notebook;

    default:
        return ItemType::Other;
    }
}
