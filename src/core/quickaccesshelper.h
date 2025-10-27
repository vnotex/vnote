#ifndef QUICKACCESSHELPER_H
#define QUICKACCESSHELPER_H

#include <QStringList>

namespace vnotex {
class QuickAccessHelper {
public:
  QuickAccessHelper() = delete;

  static void pinToQuickAccess(const QStringList &p_files);
};
} // namespace vnotex

#endif // QUICKACCESSHELPER_H
