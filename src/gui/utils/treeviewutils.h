#ifndef TREEVIEWUTILS_H
#define TREEVIEWUTILS_H

#include <QTreeView>

namespace vnotex {

// Shared indentation step for every sidebar/dock tree (Notebooks, Tags,
// Search, Outline, Tasks). Header-only on purpose: several test targets
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
};

} // namespace vnotex

#endif // TREEVIEWUTILS_H
