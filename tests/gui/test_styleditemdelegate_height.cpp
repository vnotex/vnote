// StyledItemDelegate's highlighted-segment branch bypasses CT_ItemViewItem
// entirely: it measures a QTextDocument instead. That branch is what Find United
// Entry renders every matched row with, so if it does not carry the theme's
// ::item padding the highlighted rows are a different height from their
// unhighlighted siblings in the same tree.
//
// Assertions are differential (a highlighted row against an unhighlighted one,
// and one padding rule against another), never absolute.

#include <QtTest>

#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QTreeView>

#include <core/global.h>
#include <widgets/styleditemdelegate.h>

namespace tests {

class TestStyledItemDelegateHeight : public QObject {
  Q_OBJECT

private slots:
  void highlightedRowMatchesUnhighlightedRow();
  void highlightedRowFollowsThemePadding();
  void highlightedTextStartsWhereUnhighlightedTextStarts();

private:
  // Height of a highlighted / plain row under p_styleSheet.
  static int rowHeight(const QString &p_styleSheet, bool p_highlighted);

  // x of the leftmost painted glyph column of a highlighted / plain row, or -1.
  static int firstInkColumn(const QString &p_styleSheet, bool p_highlighted);
};

int TestStyledItemDelegateHeight::rowHeight(const QString &p_styleSheet, bool p_highlighted) {
  QTreeView view;
  view.setStyleSheet(p_styleSheet);
  view.ensurePolished();

  QStandardItemModel model;
  auto *item = new QStandardItem(QStringLiteral("A matched entry"));
  if (p_highlighted) {
    QList<vnotex::Segment> segments;
    segments.append(vnotex::Segment(2, 7));
    item->setData(QVariant::fromValue(segments), vnotex::HighlightsRole);
  }
  model.appendRow(item);
  view.setModel(&model);

  vnotex::StyledItemDelegate delegate(QSharedPointer<vnotex::StyledItemDelegateInterface>(),
                                      vnotex::StyledItemDelegate::DelegateFlag::Highlights,
                                      QBrush(), QBrush());

  QStyleOptionViewItem opt;
  opt.initFrom(&view);
  opt.widget = &view;
  return delegate.sizeHint(opt, model.index(0, 0)).height();
}

void TestStyledItemDelegateHeight::highlightedRowMatchesUnhighlightedRow() {
  const QString rule = QStringLiteral("QTreeView::item { padding: 4px 8px; }");
  QCOMPARE(rowHeight(rule, true), rowHeight(rule, false));
}

void TestStyledItemDelegateHeight::highlightedRowFollowsThemePadding() {
  // The theme owns the padding, so a 4px rule must be exactly 4px taller per side
  // than a 2px one — and nothing (e.g. QTextDocument's own default 4px document
  // margin) may be counted on top of it.
  const int tight = rowHeight(QStringLiteral("QTreeView::item { padding: 2px 8px; }"), true);
  const int loose = rowHeight(QStringLiteral("QTreeView::item { padding: 4px 8px; }"), true);
  QCOMPARE(loose - tight, 4);
}

int TestStyledItemDelegateHeight::firstInkColumn(const QString &p_styleSheet, bool p_highlighted) {
  QTreeView view;
  view.setStyleSheet(p_styleSheet);
  view.ensurePolished();

  QStandardItemModel model;
  auto *item = new QStandardItem(QStringLiteral("MMMM"));
  if (p_highlighted) {
    QList<vnotex::Segment> segments;
    // The highlight format carries only brushes (SimpleSegmentHighlighter), so
    // it cannot move a glyph; only the code path differs.
    segments.append(vnotex::Segment(0, 4));
    item->setData(QVariant::fromValue(segments), vnotex::HighlightsRole);
  }
  model.appendRow(item);
  view.setModel(&model);

  vnotex::StyledItemDelegate delegate(QSharedPointer<vnotex::StyledItemDelegateInterface>(),
                                      vnotex::StyledItemDelegate::DelegateFlag::Highlights,
                                      QBrush(), QBrush());

  QStyleOptionViewItem opt;
  opt.initFrom(&view);
  opt.widget = &view;
  opt.state |= QStyle::State_Enabled;
  opt.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
  opt.rect = QRect(0, 0, 200, delegate.sizeHint(opt, model.index(0, 0)).height());

  QImage image(opt.rect.size(), QImage::Format_RGB32);
  image.fill(Qt::white); // hardcoded-color-allow: test canvas, not application chrome.
  {
    QPainter painter(&image);
    delegate.paint(&painter, opt, model.index(0, 0));
  }

  // Sample the background from the far right, which the short text never reaches.
  const QRgb background = image.pixel(image.width() - 2, image.height() / 2);
  const auto luminance = [](QRgb p_rgb) {
    return (qRed(p_rgb) * 299 + qGreen(p_rgb) * 587 + qBlue(p_rgb) * 114) / 1000;
  };
  const int backgroundLuma = luminance(background);

  for (int x = 0; x < image.width(); ++x) {
    for (int y = 0; y < image.height(); ++y) {
      if (qAbs(luminance(image.pixel(x, y)) - backgroundLuma) > 40) {
        return x;
      }
    }
  }
  return -1;
}

void TestStyledItemDelegateHeight::highlightedTextStartsWhereUnhighlightedTextStarts() {
  // SE_ItemViewItemText is only the text ALLOCATION; QCommonStyle then insets it
  // by PM_FocusFrameHMargin + 1 before drawing. Forgetting that inset shifts every
  // highlighted row a few pixels left of its unhighlighted siblings.
  const QString rule = QStringLiteral("QTreeView::item { padding: 4px 8px; }");

  // Guard against a vacuous pass: if the native inset were 0 the assertion below
  // would hold even for an implementation that ignores it entirely.
  QTreeView probe;
  const int textMargin =
      probe.style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, &probe) + 1;
  if (textMargin <= 1) {
    QSKIP("this style adds no horizontal text inset, so the regression is unobservable");
  }

  const int plain = firstInkColumn(rule, false);
  const int highlighted = firstInkColumn(rule, true);

  QVERIFY2(plain > 0, "no text was painted for the unhighlighted row");
  QVERIFY2(highlighted > 0, "no text was painted for the highlighted row");
  QVERIFY2(qAbs(highlighted - plain) <= 1,
           qPrintable(QStringLiteral("highlighted text starts at x=%1 but unhighlighted text "
                                     "starts at x=%2")
                          .arg(highlighted)
                          .arg(plain)));
}

} // namespace tests

QTEST_MAIN(tests::TestStyledItemDelegateHeight)
#include "test_styleditemdelegate_height.moc"
