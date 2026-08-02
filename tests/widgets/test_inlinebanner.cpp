#include <QtTest>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QVariant>

#include <widgets/inlinebanner.h>
#include <widgets/propertydefs.h>

using vnotex::InlineBanner;
using vnotex::PropertyDefs;

namespace tests {

class TestInlineBanner : public QObject {
  Q_OBJECT

private slots:
  void testDefaultsToInfo();
  void testConvenienceConstructor();
  void testSetTextRoundTrips();
  void testSeverityDrivesQssProperty();
  void testSeverityNameMapping();
  void testAddActionButtonReturnsUsableButton();
  void testActionButtonsAppendInOrder();
  void testClearActionButtons();
  void testStyledBackgroundAttributeIsSet();
  void testNoInlineStyleSheet();
};

void TestInlineBanner::testDefaultsToInfo() {
  InlineBanner banner;
  QCOMPARE(banner.getSeverity(), InlineBanner::Severity::Info);
  QVERIFY(banner.getText().isEmpty());
  QVERIFY(banner.getActionButtons().isEmpty());
}

void TestInlineBanner::testConvenienceConstructor() {
  InlineBanner banner(InlineBanner::Severity::Error, QStringLiteral("boom"));
  QCOMPARE(banner.getSeverity(), InlineBanner::Severity::Error);
  QCOMPARE(banner.getText(), QStringLiteral("boom"));
  QCOMPARE(banner.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("error"));
}

void TestInlineBanner::testSetTextRoundTrips() {
  InlineBanner banner;
  banner.setText(QStringLiteral("hello"));
  QCOMPARE(banner.getText(), QStringLiteral("hello"));

  // The message must live in a QLabel so the themes' `InlineBanner QLabel`
  // rule has something to match, and so long text wraps.
  auto labels = banner.findChildren<QLabel *>();
  QCOMPARE(labels.size(), 1);
  QCOMPARE(labels.at(0)->text(), QStringLiteral("hello"));
  QVERIFY(labels.at(0)->wordWrap());
}

// The whole point of the widget: severity must reach QSS as a dynamic
// property, because that is the only thing the per-theme interface.qss rules
// can select on.
void TestInlineBanner::testSeverityDrivesQssProperty() {
  InlineBanner banner;
  QCOMPARE(banner.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("info"));

  banner.setSeverity(InlineBanner::Severity::Warning);
  QCOMPARE(banner.getSeverity(), InlineBanner::Severity::Warning);
  QCOMPARE(banner.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("warning"));

  banner.setSeverity(InlineBanner::Severity::Error);
  QCOMPARE(banner.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("error"));

  banner.setSeverity(InlineBanner::Severity::Info);
  QCOMPARE(banner.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("info"));
}

// The exact strings the 10 interface.qss files select on. Changing one without
// updating every theme silently drops the styling.
void TestInlineBanner::testSeverityNameMapping() {
  QCOMPARE(InlineBanner::severityName(InlineBanner::Severity::Info), QStringLiteral("info"));
  QCOMPARE(InlineBanner::severityName(InlineBanner::Severity::Warning), QStringLiteral("warning"));
  QCOMPARE(InlineBanner::severityName(InlineBanner::Severity::Error), QStringLiteral("error"));
}

void TestInlineBanner::testAddActionButtonReturnsUsableButton() {
  InlineBanner banner;
  auto *btn = banner.addActionButton(QStringLiteral("Do It"));
  QVERIFY(btn);
  QCOMPARE(btn->text(), QStringLiteral("Do It"));
  QCOMPARE(btn->parent(), &banner);

  // The caller owns the semantics: the returned button must be connectable.
  QSignalSpy spy(btn, &QPushButton::clicked);
  btn->click();
  QCOMPARE(spy.count(), 1);
}

void TestInlineBanner::testActionButtonsAppendInOrder() {
  InlineBanner banner;
  auto *a = banner.addActionButton(QStringLiteral("A"));
  auto *b = banner.addActionButton(QStringLiteral("B"));
  auto *c = banner.addActionButton(QStringLiteral("C"));

  const auto buttons = banner.getActionButtons();
  QCOMPARE(buttons.size(), 3);
  QCOMPARE(buttons.at(0), a);
  QCOMPARE(buttons.at(1), b);
  QCOMPARE(buttons.at(2), c);

  // Layout order must match insertion order, after the stretching label.
  auto *layout = qobject_cast<QHBoxLayout *>(banner.layout());
  QVERIFY(layout);
  QCOMPARE(layout->count(), 4);
  QCOMPARE(layout->itemAt(1)->widget(), a);
  QCOMPARE(layout->itemAt(2)->widget(), b);
  QCOMPARE(layout->itemAt(3)->widget(), c);
  // The label takes the slack so the buttons stay right-aligned.
  QCOMPARE(layout->stretch(0), 1);
}

void TestInlineBanner::testClearActionButtons() {
  InlineBanner banner;
  banner.addActionButton(QStringLiteral("A"));
  banner.addActionButton(QStringLiteral("B"));
  QCOMPARE(banner.getActionButtons().size(), 2);

  banner.clearActionButtons();
  QCOMPARE(banner.getActionButtons().size(), 0);

  auto *layout = qobject_cast<QHBoxLayout *>(banner.layout());
  QVERIFY(layout);
  QCOMPARE(layout->count(), 1); // Only the label is left.

  // deleteLater() is pending; drain it so the children are really gone.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCOMPARE(banner.findChildren<QPushButton *>().size(), 0);

  // Re-adding after a clear still works.
  QVERIFY(banner.addActionButton(QStringLiteral("C")));
  QCOMPARE(banner.getActionButtons().size(), 1);
}

// Without WA_StyledBackground a bare QFrame subclass silently ignores the
// background-color coming from the global stylesheet.
void TestInlineBanner::testStyledBackgroundAttributeIsSet() {
  InlineBanner banner;
  QVERIFY(banner.testAttribute(Qt::WA_StyledBackground));
}

// Regression gate for the defect this widget exists to fix: colors must come
// from the theme, never from a hardcoded per-instance stylesheet.
void TestInlineBanner::testNoInlineStyleSheet() {
  InlineBanner banner(InlineBanner::Severity::Warning, QStringLiteral("x"));
  banner.addActionButton(QStringLiteral("A"));

  QVERIFY2(banner.styleSheet().isEmpty(), "InlineBanner must not set an inline stylesheet");
  for (auto *child : banner.findChildren<QWidget *>()) {
    QVERIFY2(child->styleSheet().isEmpty(),
             qPrintable(QStringLiteral("child %1 must not set an inline stylesheet")
                            .arg(QString::fromUtf8(child->metaObject()->className()))));
  }
}

} // namespace tests

QTEST_MAIN(tests::TestInlineBanner)
#include "test_inlinebanner.moc"
