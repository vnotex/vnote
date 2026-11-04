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

  if (!text.startsWith(QLatin1Char('>'))) {
    entry.m_name = QStringLiteral("find");
    // Keep the entire query positional and quoted, including tabs and literal quotes.
    entry.m_args = QStringLiteral("--scope all_notebook --object name -- ");
    entry.m_args.reserve(entry.m_args.size() + text.size() + 2 * text.count(QLatin1Char('"')) + 2);
    entry.m_args += QLatin1Char('"');
    for (const auto ch : text) {
      if (ch == QLatin1Char('"')) {
        entry.m_args += QStringLiteral("\"\"\"");
      } else {
        entry.m_args += ch;
      }
    }
    entry.m_args += QLatin1Char('"');
    return entry;
  }

  entry.m_isCommand = true;
  const auto command = QStringView(text).mid(1).trimmed();
  qsizetype idx = 0;
  while (idx < command.size() && !command.at(idx).isSpace()) {
    ++idx;
  }
  entry.m_name = command.left(idx).toString();
  entry.m_args = command.mid(idx).trimmed().toString();

  return entry;
}

const QIcon &UnitedEntryHelper::itemIcon(ItemType p_type) const { return m_icons[p_type]; }

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
