// Stub for vnotex::ThemeService::paletteColor used by test_notebook_selector2_readonly_badge.
//
// NotebookSelector2 inherits from NavigationMode whose
// generateNavigationLabelStyle() calls ThemeService::paletteColor. The
// selector's read-only badge path (addNotebookItem → isNotebookReadOnly →
// readOnlyBadgeIcon) never triggers navigation, so the call is dead at
// runtime. The MSVC linker still requires the symbol to resolve.
//
// Linking the real themeservice.cpp would drag in Theme, palette JSON
// parsing, and a dozen other widget-tier dependencies. A trivial stub
// satisfies the linker without bloating the test binary.

#include <QString>

#include <gui/services/themeservice.h>

namespace vnotex {

QString ThemeService::paletteColor(const QString &p_name) const {
  Q_UNUSED(p_name);
  return QString();
}

} // namespace vnotex
