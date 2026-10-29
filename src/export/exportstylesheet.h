#ifndef EXPORTSTYLESHEET_H
#define EXPORTSTYLESHEET_H

#include <QString>

namespace vnotex {
class ConfigMgr2;

class ExportStyleSheet {
public:
  ExportStyleSheet() = delete;

  // Seed only when absent; existing customizations, including an empty file, are authoritative.
  static QString ensureUserFile(ConfigMgr2 &p_config, QString *p_error = nullptr);

  // Apply the saved stylesheet to completed HTML. Native-only callers use the bundled default.
  static bool apply(const QString &p_htmlFile, ConfigMgr2 *p_config, QString *p_error = nullptr);
};
} // namespace vnotex

#endif // EXPORTSTYLESHEET_H
