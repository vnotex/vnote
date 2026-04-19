#ifndef UNITEDENTRYHELPER_H
#define UNITEDENTRYHELPER_H

#include <QIcon>
#include <QString>

#include <core/location.h>

namespace vnotex {

class ThemeService;

class UnitedEntryHelper {
public:
  struct UserEntry {
    QString m_name;

    QString m_args;
  };

  enum ItemType { Buffer, File, Folder, Notebook, Other, MaxItemType };

  explicit UnitedEntryHelper(ThemeService *p_themeService);

  const QIcon &itemIcon(ItemType p_type) const;

  void refreshIcons();

  static UserEntry parseUserEntry(const QString &p_text);

  static ItemType locationTypeToItemType(LocationType p_type);

private:
  ThemeService *m_themeService = nullptr;

  QIcon m_icons[MaxItemType];
};

} // namespace vnotex

#endif // UNITEDENTRYHELPER_H
