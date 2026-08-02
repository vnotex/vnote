#include <QtTest>

#include <QPushButton>
#include <QSignalSpy>
#include <QString>

#include <widgets/inlinebanner.h>
#include <widgets/legacyimagemigrationbar.h>
#include <widgets/propertydefs.h>

using vnotex::InlineBanner;
using vnotex::LegacyImageMigrationBar;
using vnotex::PropertyDefs;

namespace tests {

// Pins the View wiring of the legacy-image migration prompt. The migration
// policy itself is covered by test_legacy_image_migration; this only asserts
// that the three buttons still map to the three intents MarkdownViewWindow2
// connects to, which is what the InlineBanner extraction rewrote.
class TestLegacyImageMigrationBar : public QObject {
  Q_OBJECT

private slots:
  void testIsAWarningBanner();
  void testThreeActionsInOrder();
  void testButtonsEmitTheirIntents();
  void testImageCountUpdatesText();
  void testNoInlineStyleSheet();
};

void TestLegacyImageMigrationBar::testIsAWarningBanner() {
  LegacyImageMigrationBar bar;
  QCOMPARE(bar.getSeverity(), InlineBanner::Severity::Warning);
  // The severity has to reach QSS, or every theme renders it as info.
  QCOMPARE(bar.property(PropertyDefs::c_bannerSeverity).toString(), QStringLiteral("warning"));
}

void TestLegacyImageMigrationBar::testThreeActionsInOrder() {
  LegacyImageMigrationBar bar;
  const auto buttons = bar.getActionButtons();
  QCOMPARE(buttons.size(), 3);
  // Migrate first: the affirmative action leads.
  QVERIFY(buttons.at(0)->text().contains(QStringLiteral("Migrate")));
  QVERIFY(!buttons.at(1)->text().isEmpty());
  QVERIFY(!buttons.at(2)->text().isEmpty());
}

void TestLegacyImageMigrationBar::testButtonsEmitTheirIntents() {
  LegacyImageMigrationBar bar;
  const auto buttons = bar.getActionButtons();
  QCOMPARE(buttons.size(), 3);

  QSignalSpy migrate(&bar, &LegacyImageMigrationBar::migrateRequested);
  QSignalSpy dismiss(&bar, &LegacyImageMigrationBar::dismissRequested);
  QSignalSpy never(&bar, &LegacyImageMigrationBar::neverRequested);

  buttons.at(0)->click();
  QCOMPARE(migrate.count(), 1);
  QCOMPARE(dismiss.count(), 0);
  QCOMPARE(never.count(), 0);

  buttons.at(1)->click();
  QCOMPARE(migrate.count(), 1);
  QCOMPARE(dismiss.count(), 1);
  QCOMPARE(never.count(), 0);

  buttons.at(2)->click();
  QCOMPARE(migrate.count(), 1);
  QCOMPARE(dismiss.count(), 1);
  QCOMPARE(never.count(), 1);
}

void TestLegacyImageMigrationBar::testImageCountUpdatesText() {
  LegacyImageMigrationBar bar;
  QVERIFY(!bar.getText().isEmpty());

  bar.setImageCount(1);
  const QString one = bar.getText();
  bar.setImageCount(7);
  const QString many = bar.getText();

  QVERIFY(!one.isEmpty());
  QVERIFY(!many.isEmpty());
  QVERIFY2(many.contains(QStringLiteral("7")),
           qPrintable(QStringLiteral("count should appear in the message; got: %1").arg(many)));
  // The message must warn that files move on disk, since that is the
  // irreversible part the user is consenting to.
  QVERIFY(many.contains(QStringLiteral("disk")));
}

void TestLegacyImageMigrationBar::testNoInlineStyleSheet() {
  LegacyImageMigrationBar bar;
  QVERIFY2(bar.styleSheet().isEmpty(),
           "the bar must take its colors from the theme, not a hardcoded stylesheet");
  for (auto *child : bar.findChildren<QWidget *>()) {
    QVERIFY(child->styleSheet().isEmpty());
  }
}

} // namespace tests

QTEST_MAIN(tests::TestLegacyImageMigrationBar)
#include "test_legacyimagemigrationbar.moc"
