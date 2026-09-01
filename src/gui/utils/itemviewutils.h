#ifndef ITEMVIEWUTILS_H
#define ITEMVIEWUTILS_H

#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QtGlobal>

namespace vnotex {

// The rule: a custom item delegate owns the CONTENT height of a row; the active
// theme owns the PADDING around it.
//
// Qt applies a QSS `::item { padding: ...; }` box inside
// QStyleSheetStyle::sizeFromContents(CT_ItemViewItem, ...), which is reachable
// only through QStyledItemDelegate::sizeHint(). A delegate that computes its own
// height never gets there, so the theme's padding is silently dropped and the
// dock rows drift apart (and `native`'s deliberately tighter 2px never applies).
// verticalChrome() gives such a delegate the same figure Qt would have added.
//
// Header-only on purpose, exactly like TreeViewUtils (treeviewutils.h): several
// test targets compile these delegates without linking any gui/utils source, so
// an out-of-line definition would break them with an unresolved symbol.
class ItemViewUtils {
public:
  ItemViewUtils() = delete;

  // Vertical chrome (stylesheet padding/border plus whatever the native style
  // adds) around ONE line of item content, for the style and stylesheet in
  // effect on p_option.widget.
  //
  // p_option MUST already have been through the delegate's initStyleOption():
  // initStyleOption() is protected, so only the delegate itself can call it.
  // Callers must also do their own content maths with p_option.fontMetrics
  // rather than the raw view option, so that Qt::FontRole is respected.
  //
  // The measurement is DIFFERENTIAL, against a controlled synthetic probe:
  // subtracting qMax(fontHeight, decorationSize) from the real option would be
  // wrong whenever the index exposes no Qt::DecorationRole (true for
  // NotebookNodeModel and SearchResultModel, which paint their icons privately),
  // whenever a check indicator dominates, and whenever QCommonStyle adds its
  // icon-driven +2px. The probe has no decoration and no check indicator, so its
  // native content height IS its font height and the subtraction isolates the
  // chrome.
  //
  // SYMMETRY ASSUMPTION: the returned figure is a single combined top+bottom
  // value, because neither sizeFromContents() nor any other public style API
  // exposes the two separately. Every shipped theme uses symmetric
  // `padding: Npx Mpx`, so paint paths that need a top offset use chrome / 2. If
  // an asymmetric theme ever appears, this must be split into a
  // QMargins contentMargins() derived from
  // QStyleSheetStyle::subElementRect(SE_ItemViewItemText, ...).
  //
  // A stylesheet `min-height`/`height` on ::item would be absorbed into the
  // returned value. No shipped theme sets either.
  static int verticalChrome(const QStyleOptionViewItem &p_option) {
    QStyleOptionViewItem probe(p_option);
    probe.features = QStyleOptionViewItem::HasDisplay;
    probe.icon = QIcon();
    probe.text = QStringLiteral("X");
    probe.rect = QRect();

    QStyle *style = probe.widget ? probe.widget->style() : QApplication::style();
    if (!style) {
      return 0;
    }

    const int measured =
        style->sizeFromContents(QStyle::CT_ItemViewItem, &probe, QSize(), probe.widget).height();
    return qMax(0, measured - probe.fontMetrics.height());
  }
};

} // namespace vnotex

#endif // ITEMVIEWUTILS_H
