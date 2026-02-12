#ifndef THEMEUTILS_H
#define THEMEUTILS_H

#include <QJsonObject>
#include <QPixmap>
#include <QString>

namespace vnotex {

// GUI-dependent theme utilities
// These functions were moved from Theme class to separate GUI concerns
class ThemeUtils {
public:
  ThemeUtils() = delete;

  // Load theme cover image from folder
  // Returns empty QPixmap if cover.png doesn't exist
  static QPixmap getCover(const QString &p_folder);

  // Get cover image file path (for lazy loading)
  static QString getCoverPath(const QString &p_folder);

  // Backfill system palette colors into theme palette object
  // Uses QApplication::palette() to get current system colors
  static QJsonObject backfillSystemPalette(QJsonObject p_obj);
};

} // namespace vnotex

#endif // THEMEUTILS_H
