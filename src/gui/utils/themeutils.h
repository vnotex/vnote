#ifndef THEMEUTILS_H
#define THEMEUTILS_H

#include <QJsonObject>
#include <QString>

namespace vnotex {

// GUI-dependent theme utilities
// These functions were moved from Theme class to separate GUI concerns
class ThemeUtils {
public:
  ThemeUtils() = delete;

  // Backfill system palette colors into theme palette object
  // Uses QApplication::palette() to get current system colors
  static QJsonObject backfillSystemPalette(QJsonObject p_obj);
};

} // namespace vnotex

#endif // THEMEUTILS_H
