// Stubs for symbols referenced by tagviewer2.cpp / listwidget.cpp but not
// needed by the tests. Avoids pulling in deep cascading widget dependencies.

#include <QKeyEvent>
#include <QString>
#include <QWidget>

#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>

using namespace vnotex;

// --- WidgetUtils stubs ---

bool WidgetUtils::processKeyEventLikeVi(QWidget *p_widget, QKeyEvent *p_event,
                                        QWidget *p_escTargetWidget) {
  Q_UNUSED(p_widget);
  Q_UNUSED(p_event);
  Q_UNUSED(p_escTargetWidget);
  return false;
}

// --- ThemeService stubs ---
// These are referenced by tagviewer2.cpp and listwidget.cpp but never called
// because ThemeService is not registered in the test ServiceLocator.

QString ThemeService::getIconFile(const QString &p_icon) const {
  Q_UNUSED(p_icon);
  return QString();
}

QString ThemeService::paletteColor(const QString &p_name) const {
  Q_UNUSED(p_name);
  return QString();
}
