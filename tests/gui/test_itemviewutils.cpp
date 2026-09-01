// Behavioural coverage of vnotex::ItemViewUtils::verticalChrome().
//
// Every assertion here is DIFFERENTIAL — "chrome with a `::item` padding rule"
// minus "chrome with no stylesheet". Asserting an absolute value (in particular
// "chrome == 0 when there is no stylesheet") would be wrong: the native style
// legitimately contributes to CT_ItemViewItem, and QCommonStyle additionally
// adds 2px when an icon determines the item height.
//
// The grep gate that keeps the five overriding delegates wired to this helper is
// tests/utils/test_itemheight_drift.cpp.

#include <QtTest>

#include <QFrame>
#include <QIcon>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QVBoxLayout>

#include <gui/utils/itemviewutils.h>

namespace tests {

class TestItemViewUtils : public QObject {
  Q_OBJECT

private slots:
  void paddingRuleIsReflectedInChrome();
  void paddingRuleIsReflectedInChrome_data();
  void borderRuleIsReflectedInChrome();
  void chromeIsIndependentOfDecorationAndCheckState();
  void scopedSelectorOnlyAppliesToMatchingWidget();

private:
  // Chrome for a QTreeView carrying p_styleSheet.
  static int chromeForStyleSheet(const QString &p_styleSheet);
  static QStyleOptionViewItem optionFor(const QWidget *p_widget);
};

QStyleOptionViewItem TestItemViewUtils::optionFor(const QWidget *p_widget) {
  QStyleOptionViewItem opt;
  opt.initFrom(p_widget);
  opt.widget = p_widget;
  opt.features = QStyleOptionViewItem::HasDisplay;
  opt.text = QStringLiteral("Sample");
  opt.decorationSize = QSize(16, 16);
  return opt;
}

int TestItemViewUtils::chromeForStyleSheet(const QString &p_styleSheet) {
  QTreeView view;
  view.setStyleSheet(p_styleSheet);
  view.ensurePolished();
  return vnotex::ItemViewUtils::verticalChrome(optionFor(&view));
}

void TestItemViewUtils::paddingRuleIsReflectedInChrome_data() {
  QTest::addColumn<QString>("rule");
  QTest::addColumn<int>("expectedDelta");

  QTest::newRow("4px vertical (the 11 shipped themes)")
      << QStringLiteral("QTreeView::item { padding: 4px 8px; }") << 8;
  QTest::newRow("2px vertical (native's intent)")
      << QStringLiteral("QTreeView::item { padding: 2px 8px; }") << 4;
  QTest::newRow("no vertical padding")
      << QStringLiteral("QTreeView::item { padding: 0px 8px; }") << 0;
}

void TestItemViewUtils::paddingRuleIsReflectedInChrome() {
  QFETCH(QString, rule);
  QFETCH(int, expectedDelta);

  const int baseline = chromeForStyleSheet(QString());
  const int themed = chromeForStyleSheet(rule);
  QCOMPARE(themed - baseline, expectedDelta);
}

void TestItemViewUtils::borderRuleIsReflectedInChrome() {
  // A border is chrome too: it is part of the ::item box and pushes the content
  // down exactly like padding does.
  const int noBorder = chromeForStyleSheet(QStringLiteral("QTreeView::item { padding: 0px; }"));
  const int withBorder = chromeForStyleSheet(
      QStringLiteral("QTreeView::item { padding: 0px; border: 1px solid palette(text); }"));
  QCOMPARE(withBorder - noBorder, 2);
}

void TestItemViewUtils::chromeIsIndependentOfDecorationAndCheckState() {
  // The measurement is differential against a synthetic probe precisely so that
  // an index's decoration / check indicator cannot leak into the figure. Several
  // of the migrated delegates paint their icon privately and expose no
  // Qt::DecorationRole at all, so any decoration-derived baseline would
  // over-subtract for them.
  const QString rule = QStringLiteral("QTreeView::item { padding: 4px 8px; }");

  QTreeView view;
  view.setStyleSheet(rule);
  view.ensurePolished();

  const QStyleOptionViewItem plain = optionFor(&view);
  const int plainChrome = vnotex::ItemViewUtils::verticalChrome(plain);

  QPixmap pm(32, 32);
  pm.fill(Qt::red); // hardcoded-color-allow: test fixture pixmap, not chrome.

  QStyleOptionViewItem decorated(plain);
  decorated.features |= QStyleOptionViewItem::HasDecoration;
  decorated.icon = QIcon(pm);
  decorated.decorationSize = QSize(32, 32);
  QCOMPARE(vnotex::ItemViewUtils::verticalChrome(decorated), plainChrome);

  QStyleOptionViewItem nullIcon(plain);
  nullIcon.features |= QStyleOptionViewItem::HasDecoration;
  nullIcon.icon = QIcon();
  QCOMPARE(vnotex::ItemViewUtils::verticalChrome(nullIcon), plainChrome);

  QStyleOptionViewItem checkable(plain);
  checkable.features |= QStyleOptionViewItem::HasCheckIndicator;
  checkable.checkState = Qt::Checked;
  QCOMPARE(vnotex::ItemViewUtils::verticalChrome(checkable), plainChrome);
}

void TestItemViewUtils::scopedSelectorOnlyAppliesToMatchingWidget() {
  // `native` scopes its 2px rule to specific docks rather than styling every
  // ::item, so the helper has to resolve the selector against the real widget
  // rather than against "some QTreeView".
  QWidget window;
  window.setStyleSheet(QStringLiteral("QFrame QTreeView::item { padding: 4px 8px; }"));

  auto *layout = new QVBoxLayout(&window);

  auto *frame = new QFrame(&window);
  auto *frameLayout = new QVBoxLayout(frame);
  auto *matching = new QTreeView(frame);
  frameLayout->addWidget(matching);
  layout->addWidget(frame);

  auto *nonMatching = new QTreeView(&window);
  layout->addWidget(nonMatching);

  window.ensurePolished();
  matching->ensurePolished();
  nonMatching->ensurePolished();

  const int baseline = chromeForStyleSheet(QString());
  QCOMPARE(vnotex::ItemViewUtils::verticalChrome(optionFor(matching)) - baseline, 8);
  QCOMPARE(vnotex::ItemViewUtils::verticalChrome(optionFor(nonMatching)) - baseline, 0);
}

} // namespace tests

QTEST_MAIN(tests::TestItemViewUtils)
#include "test_itemviewutils.moc"
