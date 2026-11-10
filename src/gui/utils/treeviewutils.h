#ifndef TREEVIEWUTILS_H
#define TREEVIEWUTILS_H

#include <QTreeView>
#include <QVariant>

#include "itemviewutils.h"

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

  struct HorizontalSpacing {
    int m_left;
    int m_right;
    int m_gap;
  };

  static HorizontalSpacing horizontalSpacing(const QStyleOptionViewItem &p_option, int p_default) {
    const QWidget *widget = p_option.widget;
    if (!widget || !widget->property(c_compactProperty).toBool()) {
      return {p_default, p_default, p_default};
    }

    // Custom delegates own their icons/checks. Measure the undecorated text slot
    // so the outer insets match standard tree text without counting an icon twice.
    QStyleOptionViewItem probe(p_option);
    probe.features = QStyleOptionViewItem::HasDisplay;
    probe.icon = QIcon();
    probe.decorationSize = QSize();
    probe.text = QStringLiteral("X");
    QStyle *style = widget->style();
    if (!probe.rect.isValid()) {
      // sizeHint() normally receives no rectangle. Use the same style's minimum
      // item box to recover its insets without relying on an arbitrary probe size.
      probe.rect = QRect(QPoint(),
                         style->sizeFromContents(QStyle::CT_ItemViewItem, &probe, QSize(), widget));
    }
    const QRect text = ItemViewUtils::textRect(probe, style);
    return {text.left() - probe.rect.left(), probe.rect.right() - text.right(), 4};
  }

private:
  static constexpr const char *c_compactProperty = "_vnote_compactTreeSpacing";
  static constexpr const char *c_indentationProperty = "_vnote_treeIndentation";
};

} // namespace vnotex

#endif // TREEVIEWUTILS_H
