#include "stringutils.h"
#include "strnatcmp.h"

namespace vnotex {

bool naturalCompare(const QString &p_a, const QString &p_b) {
  return strnatcasecmp(p_a.toUtf8().constData(), p_b.toUtf8().constData()) < 0;
}

QString formatFileSize(quint64 p_bytes, const QLocale &p_locale) {
  static const char *const c_units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
  constexpr auto c_unitCount = sizeof(c_units) / sizeof(c_units[0]);
  size_t unit = 0;
  quint64 divisor = 1;
  // Bound the unit first: multiplying the EB divisor by 1024 would overflow.
  while (unit + 1 < c_unitCount && p_bytes / divisor >= 1024) {
    divisor *= 1024;
    ++unit;
  }
  const QString number = unit == 0
                             ? p_locale.toString(p_bytes)
                             : p_locale.toString(static_cast<double>(p_bytes) / divisor, 'f', 2);
  return number + QLatin1Char(' ') + QLatin1String(c_units[unit]);
}

} // namespace vnotex
