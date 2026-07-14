// GUI test for GreetingSticker (dashboard sticker showing a time-of-day
// greeting). NOT GUILESS: constructs a real QWidget (QLabel), so it needs
// QApplication. Verifies the factory registration, the type-id, and the pure
// greetingForHour(int) hour->string mapping (clock-free, so no time mocking).

#include <QtTest>

#include <QJsonObject>
#include <QLabel>
#include <QShowEvent>
#include <QTime>

#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>
#include <widgets/dashboard/greetingsticker.h>

using namespace vnotex;

namespace tests {

class TestGreetingSticker : public QObject {
  Q_OBJECT

private slots:
  void test_typeIdAndTitle();
  void test_factoryRegistersGreeting();
  void test_greetingForHour();
  void test_greetingForHour_data();
  void test_showUpdatesLabel();
};

void TestGreetingSticker::test_typeIdAndTitle() {
  ServiceLocator services;
  GreetingSticker sticker(services);
  QCOMPARE(sticker.typeId(), QStringLiteral("greeting"));
  QCOMPARE(sticker.titleText(), QStringLiteral("Greetings"));
}

void TestGreetingSticker::test_factoryRegistersGreeting() {
  ServiceLocator services;
  StickerFactory factory;
  factory.registerBuiltInCreators();
  QVERIFY(factory.registeredTypes().contains(QStringLiteral("greeting")));

  Sticker *sticker =
      factory.create(QStringLiteral("greeting"), services, QJsonObject());
  QVERIFY(sticker != nullptr);
  QCOMPARE(sticker->typeId(), QStringLiteral("greeting"));
  delete sticker;
}

void TestGreetingSticker::test_greetingForHour_data() {
  QTest::addColumn<int>("hour");
  QTest::addColumn<QString>("expected");

  QTest::newRow("midnight") << 0 << QStringLiteral("Good evening!");
  QTest::newRow("pre-dawn") << 4 << QStringLiteral("Good evening!");
  QTest::newRow("morning-start") << 5 << QStringLiteral("Good morning!");
  QTest::newRow("late-morning") << 11 << QStringLiteral("Good morning!");
  QTest::newRow("noon") << 12 << QStringLiteral("Good afternoon!");
  QTest::newRow("late-afternoon") << 17 << QStringLiteral("Good afternoon!");
  QTest::newRow("evening-start") << 18 << QStringLiteral("Good evening!");
  QTest::newRow("night") << 23 << QStringLiteral("Good evening!");
}

void TestGreetingSticker::test_greetingForHour() {
  QFETCH(int, hour);
  QFETCH(QString, expected);
  QCOMPARE(GreetingSticker::greetingForHour(hour), expected);
}

void TestGreetingSticker::test_showUpdatesLabel() {
  ServiceLocator services;
  GreetingSticker sticker(services);

  auto *label = sticker.findChild<QLabel *>();
  QVERIFY(label != nullptr);
  QVERIFY(label->text().isEmpty());

  // Bracket the show with clock reads: if the event crosses a greeting boundary
  // the label may match either hour's greeting, so accept both to avoid a rare
  // time-boundary flake. The data-driven test verifies the exact mapping.
  const int hourBefore = QTime::currentTime().hour();
  QShowEvent event;
  QApplication::sendEvent(&sticker, &event);
  const int hourAfter = QTime::currentTime().hour();

  const QString expectedBefore = GreetingSticker::greetingForHour(hourBefore);
  const QString expectedAfter = GreetingSticker::greetingForHour(hourAfter);
  QVERIFY(label->text() == expectedBefore || label->text() == expectedAfter);
  sticker.hide();
}

} // namespace tests

QTEST_MAIN(tests::TestGreetingSticker)
#include "test_greetingsticker.moc"
