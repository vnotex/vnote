#include <QtTest>

#include <utils/autostartutils.h>

namespace tests {

class TestAutoStartUtils : public QObject {
  Q_OBJECT

private slots:
  void testDecideTruthTable();
  void testDecidePresentButEmpty();
  void testExpectedCommand();
  void testIsSupportedMatchesPlatform();
  void testNonWindowsBehavior();
};

void TestAutoStartUtils::testDecideTruthTable() {
  using vnotex::AutoStartAction;
  using vnotex::AutoStartUtils;

  const QString expected = QStringLiteral("\"C:\\Apps\\VNote\\vnote.exe\"");
  const QString other = QStringLiteral("\"C:\\Old\\vnote.exe\"");

  QCOMPARE(AutoStartUtils::decide(true, false, QString(), expected), AutoStartAction::Write);
  QCOMPARE(AutoStartUtils::decide(true, true, expected, expected), AutoStartAction::None);
  QCOMPARE(AutoStartUtils::decide(true, true, other, expected), AutoStartAction::Write);
  QCOMPARE(AutoStartUtils::decide(false, false, QString(), expected), AutoStartAction::None);
  QCOMPARE(AutoStartUtils::decide(false, true, expected, expected), AutoStartAction::Remove);
  QCOMPARE(AutoStartUtils::decide(false, true, other, expected), AutoStartAction::Remove);
}

void TestAutoStartUtils::testDecidePresentButEmpty() {
  using vnotex::AutoStartAction;
  using vnotex::AutoStartUtils;

  const QString expected = QStringLiteral("\"C:\\Apps\\VNote\\vnote.exe\"");
  // Present but empty must not read as matching.
  QCOMPARE(AutoStartUtils::decide(true, true, QString(), expected), AutoStartAction::Write);
  QCOMPARE(AutoStartUtils::decide(false, true, QString(), expected), AutoStartAction::Remove);
}

void TestAutoStartUtils::testExpectedCommand() {
  const QString cmd = vnotex::AutoStartUtils::expectedCommand();
  QVERIFY(!cmd.isEmpty());
  QVERIFY(cmd.startsWith(QLatin1Char('"')));
  QVERIFY(cmd.endsWith(QLatin1Char('"')));
#if defined(Q_OS_WIN)
  QVERIFY(!cmd.contains(QLatin1Char('/')));
#endif
}

void TestAutoStartUtils::testIsSupportedMatchesPlatform() {
#if defined(Q_OS_WIN)
  QVERIFY(vnotex::AutoStartUtils::isSupported());
#else
  QVERIFY(!vnotex::AutoStartUtils::isSupported());
#endif
}

void TestAutoStartUtils::testNonWindowsBehavior() {
#if defined(Q_OS_WIN)
  QSKIP("Windows registry behavior is covered by the manual checklist.");
#else
  QVERIFY(!vnotex::AutoStartUtils::hasRegisteredCommand());
  QVERIFY(vnotex::AutoStartUtils::registeredCommand().isEmpty());
  QVERIFY(!vnotex::AutoStartUtils::setEnabled(true));
  QVERIFY(vnotex::AutoStartUtils::reconcile(true));
  QVERIFY(vnotex::AutoStartUtils::reconcile(false));
#endif
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestAutoStartUtils)
#include "test_autostartutils.moc"
