#include "unitedentryhelper.h"

#include <QIcon>

#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

using namespace vnotex;

UnitedEntryHelper::UserEntry UnitedEntryHelper::parseUserEntry(const QString &p_text) {
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

const QIcon &UnitedEntryHelper::itemIcon(ItemType p_type, ThemeService *p_themeService) {
  static QIcon icons[ItemType::MaxItemType];

  if (icons[0].isNull()) {
    // Init.
    const QString nodeIconFgName = "base#icon#fg";
    const auto fg = p_themeService->paletteColor(nodeIconFgName);

    icons[ItemType::Buffer] = IconUtils::fetchIcon(p_themeService->getIconFile("buffer.svg"), fg);
    icons[ItemType::File] = IconUtils::fetchIcon(p_themeService->getIconFile("file_node.svg"), fg);
    icons[ItemType::Folder] =
        IconUtils::fetchIcon(p_themeService->getIconFile("folder_node.svg"), fg);
    icons[ItemType::Notebook] =
        IconUtils::fetchIcon(p_themeService->getIconFile("notebook_default.svg"), fg);
    icons[ItemType::Other] =
        IconUtils::fetchIcon(p_themeService->getIconFile("other_item.svg"), fg);
  }

  return icons[p_type];
}

UnitedEntryHelper::ItemType UnitedEntryHelper::locationTypeToItemType(LocationType p_type) {
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
