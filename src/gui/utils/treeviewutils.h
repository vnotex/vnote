#ifndef TREEVIEWUTILS_H
#define TREEVIEWUTILS_H

#include <QTreeView>
#include <QVariant>

namespace vnotex {

// Shared default indentation and theme spacing for tree views.
// Header-only on purpose: several test targets
// compile these views without linking gui/utils sources, so an out-of-line
// definition would break them with an unresolved symbol. Same pressure that
// forced WidgetUtils::showHorizontalScrollbar out of widgetutils.cpp.
class TreeViewUtils {
public:
  TreeViewUtils() = delete;

  // Returned by function, not a static data member: under C++14 a static data
  // member odr-used by QCOMPARE would need an out-of-class definition, which is
  // exactly the link footprint this header avoids.
  static int indentation() { return 16; }

  static void applyIndentation(QTreeView *p_view) {
    if (p_view) {
      p_view->setIndentation(indentation());
    }
  }

  // Called before Qt's polish/style-change handling. Remember each tree's own
  // default, not just the sidebar step: dialogs and custom trees may differ.
  static void applyThemeSpacing(QTreeView *p_view, bool p_compact) {
    const bool wasCompact = p_view->property(c_compactProperty).toBool();
    if (p_compact) {
      if (!wasCompact) {
        p_view->setProperty(c_indentationProperty, p_view->indentation());
        p_view->setProperty(c_compactProperty, true);
      }
      if (p_view->indentation() != 12) {
        p_view->setIndentation(12);
      }
    } else if (wasCompact) {
      p_view->setIndentation(p_view->property(c_indentationProperty).toInt());
      p_view->setProperty(c_compactProperty, QVariant());
      p_view->setProperty(c_indentationProperty, QVariant());
    }
  }

  static int horizontalPadding(const QWidget *p_widget, int p_default) {
    return p_widget && p_widget->property(c_compactProperty).toBool() ? 4 : p_default;
  }

private:
  static constexpr const char *c_compactProperty = "_vnote_compactTreeSpacing";
  static constexpr const char *c_indentationProperty = "_vnote_treeIndentation";
};

} // namespace vnotex

#endif // TREEVIEWUTILS_H
