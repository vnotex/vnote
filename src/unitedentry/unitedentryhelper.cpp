#include "unitedentryhelper.h"

#include <QIcon>

#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

using namespace vnotex;

UnitedEntryHelper::UnitedEntryHelper(ThemeService *p_themeService)
    : m_themeService(p_themeService) {
  refreshIcons();
}

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

const QIcon &UnitedEntryHelper::itemIcon(ItemType p_type) const {
  return m_icons[p_type];
}

void UnitedEntryHelper::refreshIcons() {
  const QString nodeIconFgName = "base#icon#fg";
  const auto fg = m_themeService->paletteColor(nodeIconFgName);

  m_icons[ItemType::Buffer] = IconUtils::fetchIcon(m_themeService->getIconFile("buffer.svg"), fg);
  m_icons[ItemType::File] = IconUtils::fetchIcon(m_themeService->getIconFile("file_node.svg"), fg);
  m_icons[ItemType::Folder] =
      IconUtils::fetchIcon(m_themeService->getIconFile("folder_node.svg"), fg);
  m_icons[ItemType::Notebook] =
      IconUtils::fetchIcon(m_themeService->getIconFile("notebook_default.svg"), fg);
  m_icons[ItemType::Other] =
      IconUtils::fetchIcon(m_themeService->getIconFile("other_item.svg"), fg);
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
