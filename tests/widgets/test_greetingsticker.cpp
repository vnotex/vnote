// Real widgets with injected clock/catalog inputs; production deadlines are unchanged.
#include <QtTest>

#include <QElapsedTimer>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>
#include <gui/services/tooltipservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dashboard/greetingsticker.h>

#include <functional>

using namespace vnotex;

namespace tests {
namespace {
QDateTime at(int p_hour, int p_minute = 0, int p_second = 0, int p_msec = 0) {
  return QDateTime(QDate(2026, 9, 23), QTime(p_hour, p_minute, p_second, p_msec));
}

QString normalGreetingForHour(int p_hour) {
  return GreetingSticker::greetingForHour(p_hour) + QLatin1Char('\n') +
         QStringLiteral("Read, write, and think");
}

class ClockedGreeting final : public GreetingSticker {
public:
  explicit ClockedGreeting(ServiceLocator &p_services) : GreetingSticker(p_services) {
    resize(300, 100);
  }

  QDateTime now = at(12);
  QElapsedTimer elapsed;
  QStringList tips{QStringLiteral("Tip A"), QStringLiteral("Tip B"), QStringLiteral("Tip C"),
                   QStringLiteral("Tip D")};
  std::function<QString()> tipSource;

  QScrollArea *area() const { return findChild<QScrollArea *>(); }
  QLabel *body() const { return qobject_cast<QLabel *>(area()->widget()); }
  QString displayedText() const {
    QStringList texts;
    for (auto *label : findChildren<QLabel *>()) {
      if (!label->isVisible()) {
        continue;
      }
      if (label->textFormat() == Qt::RichText) {
        QTextDocument document;
        document.setHtml(label->text());
        texts.append(document.toPlainText());
      } else {
        texts.append(label->text());
      }
    }
    return texts.join(QLatin1Char('\n'));
  }

  void refresh() {
    QVERIFY(QMetaObject::invokeMethod(findChild<QTimer *>(), "timeout", Qt::QueuedConnection));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  }

protected:
  QDateTime currentDateTime() const override {
    return elapsed.isValid() ? now.addMSecs(elapsed.elapsed()) : now;
  }
  QString randomTip() const override { return tipSource ? tipSource() : tips.value(m_nextTip++); }

private:
  mutable int m_nextTip = 0;
};

void populateFrame(QWidget *p_frame, QWidget *p_sticker) {
  p_frame->setFixedHeight(100);
  auto *layout = new QVBoxLayout(p_frame);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(2);
  auto *header = new QWidget(p_frame);
  header->setFixedHeight(20);
  layout->addWidget(header);
  layout->addWidget(p_sticker, 1);
}

QString longestCatalogTip() {
  QFile file(QFINDTESTDATA("../../src/data/extra/tooltips.json"));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QString longest;
  for (const auto &entry : QJsonDocument::fromJson(file.readAll()).array()) {
    const auto text = entry.toObject().value(QStringLiteral("en_US")).toString();
    if (text.size() > longest.size()) {
      longest = text;
    }
  }
  return longest;
}

struct BoardFixture {
  ServiceLocator services;
  QScrollArea outer;
  QWidget *left = nullptr;
  QWidget *right = nullptr;
  ClockedGreeting *sticker = nullptr;

  BoardFixture() {
    outer.setWidgetResizable(true);
    auto *container = new QWidget;
    auto *grid = new QGridLayout(container);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(6);
    for (int column = 0; column < 2; ++column) {
      grid->setColumnStretch(column, 1);
      grid->setColumnMinimumWidth(column, 60);
    }
    left = new QFrame(container);
    right = new QFrame(container);
    sticker = new ClockedGreeting(services);
    sticker->now = at(11, 59);
    populateFrame(left, sticker);
    populateFrame(right, new QLabel(QStringLiteral("Neighbor")));
    grid->addWidget(left, 0, 0);
    grid->addWidget(right, 0, 1);
    for (int row = 1; row < 8; ++row) {
      auto *filler = new QWidget(container);
      filler->setFixedHeight(100);
      grid->addWidget(filler, row, 0, 1, 2);
    }
    outer.setWidget(container);
    outer.resize(640, 240);
    outer.show();
  }
};

void wheelDown(QWidget *p_target) {
  const auto local = p_target->rect().center();
  QWheelEvent event(QPointF(local), QPointF(p_target->mapToGlobal(local)), QPoint(),
                    QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  // Like QtTest's mouse helpers, mark real input before notify(); sendEvent()
  // clears spontaneity, and Qt deliberately does not propagate synthetic wheels.
  QSpontaneKeyEvent::setSpontaneous(&event);
  qApp->notify(p_target, &event);
}

} // namespace

class TestGreetingSticker : public QObject {
  Q_OBJECT

private slots:
  void test_typeId();
  void test_factoryRegistersGreeting();
  void test_greetingForHour();
  void test_greetingForHour_data();
  void test_hourlyBoundaries();
  void test_reopenAndMissedHours();
  void test_realDeadlines();
  void test_realDeadlines_data();
  void test_lateActivationAndInvalidClock();
  void test_repeatedLocalHour();
  void test_localizedCatalog();
  void test_localizedCatalog_data();
  void test_failedCatalogIsCached();
  void test_failedCatalogIsCached_data();
  void test_literalAndLongText();
  void test_boardDoesNotGrow();
  void test_wheelRouting();
};

void TestGreetingSticker::test_typeId() {
  ServiceLocator services;
  GreetingSticker sticker(services);
  QCOMPARE(sticker.typeId(), QStringLiteral("greeting"));
}

void TestGreetingSticker::test_factoryRegistersGreeting() {
  ServiceLocator services;
  StickerFactory factory;
  factory.registerBuiltInCreators();
  QVERIFY(factory.registeredTypes().contains(QStringLiteral("greeting")));
  QScopedPointer<Sticker> sticker(
      factory.create(QStringLiteral("greeting"), services, QJsonObject()));
  QVERIFY(sticker);
  QCOMPARE(sticker->typeId(), QStringLiteral("greeting"));
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

void TestGreetingSticker::test_hourlyBoundaries() {
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.now = at(11, 59, 59, 999);
  sticker.show();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(11));
  sticker.now = at(12);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.refresh();
  sticker.now = at(12, 4, 59, 999);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.now = at(12, 5);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  sticker.now = at(13);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip B"));
}

void TestGreetingSticker::test_reopenAndMissedHours() {
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.now = at(10, 2);
  sticker.refresh(); // Hidden delivery must not consume the first selection.
  sticker.now = at(12, 3);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.now = at(12, 3, 30);
  sticker.hide();
  sticker.now = at(12, 4);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.now = at(12, 5);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  sticker.hide();
  sticker.now = at(12, 10);
  sticker.show();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  sticker.hide();
  sticker.now = at(14, 2);
  sticker.refresh();
  sticker.now = at(15, 2);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip B"));
  sticker.hide();
  sticker.now = sticker.now.addDays(1);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip C"));
}

void TestGreetingSticker::test_realDeadlines_data() {
  QTest::addColumn<QDateTime>("origin");
  QTest::addColumn<QString>("before");
  QTest::addColumn<QString>("after");
  QTest::newRow("noon") << at(11, 59, 59, 900) << normalGreetingForHour(11)
                        << QStringLiteral("Tip A");
  QTest::newRow("expiry") << at(12, 4, 59, 900) << QStringLiteral("Tip A")
                          << normalGreetingForHour(12);
}

void TestGreetingSticker::test_realDeadlines() {
  QFETCH(QDateTime, origin);
  QFETCH(QString, before);
  QFETCH(QString, after);
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.now = origin;
  // Start advancing only after the synchronous show, so native window creation
  // cannot race the initial assertion on a busy CI host.
  sticker.show();
  QCOMPARE(sticker.displayedText(), before);
  sticker.elapsed.start();
  QTRY_COMPARE_WITH_TIMEOUT(sticker.displayedText(), after, 2000);

  auto *pending = new ClockedGreeting(services);
  pending->now = at(12, 4, 59, 900);
  pending->show();
  QPointer<QLabel> label(pending->body());
  QPointer<QTimer> timer(pending->findChild<QTimer *>());
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::QueuedConnection));
  delete pending;
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  QTest::qWait(150);
  QVERIFY(label.isNull());
  QVERIFY(timer.isNull());
}

void TestGreetingSticker::test_lateActivationAndInvalidClock() {
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.now = at(12, 4);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.now = at(12, 7);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  sticker.now = at(18, 2);
  QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
                                    Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip B"));
  sticker.now = QDateTime();
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip B"));
  sticker.hide();
  sticker.now = at(19, 2);
  QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
                                    Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
  sticker.now = at(20, 2);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip C"));
}

void TestGreetingSticker::test_repeatedLocalHour() {
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.now = QDateTime::fromString(QStringLiteral("2026-11-01T01:02:00-04:00"), Qt::ISODate);
  sticker.show();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip A"));
  sticker.now = QDateTime::fromString(QStringLiteral("2026-11-01T01:02:00-05:00"), Qt::ISODate);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Tip B"));
}

void TestGreetingSticker::test_localizedCatalog_data() {
  QTest::addColumn<QString>("locale");
  QTest::addColumn<QString>("expected");
  QTest::newRow("chinese") << QStringLiteral("zh_CN") << QStringLiteral("中文");
  QTest::newRow("fallback") << QStringLiteral("ja_JP") << QStringLiteral("English");
}

void TestGreetingSticker::test_localizedCatalog() {
  QFETCH(QString, locale);
  QFETCH(QString, expected);
  const QLocale previousLocale;
  const auto restore = qScopeGuard([&] { QLocale::setDefault(previousLocale); });
  QLocale::setDefault(QLocale(locale));
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const auto path =
      tmp.createFile(QStringLiteral("tips.json"),
                     R"([null,17,{}, {"en_US":" "},{"en_US":" English ","zh_CN":" 中文 "}])");
  QCOMPARE(ToolTipService::randomTip(path), expected);
  ServiceLocator services; // No ConfigMgr2 or NotificationService is registered.
  ClockedGreeting sticker(services);
  sticker.tipSource = [&] { return ToolTipService::randomTip(path); };
  sticker.show();
  QCOMPARE(sticker.displayedText(), expected);
  const auto pair =
      tmp.createFile(QStringLiteral("pair.json"), R"([{"en_US":"One"},{"en_US":"Two"}])");
  const auto selected = ToolTipService::randomTip(pair);
  QVERIFY(selected == QStringLiteral("One") || selected == QStringLiteral("Two"));
}

void TestGreetingSticker::test_failedCatalogIsCached_data() {
  QTest::addColumn<QByteArray>("catalog");
  QTest::newRow("missing") << QByteArray();
  QTest::newRow("malformed") << QByteArray("{");
  QTest::newRow("wrong-root") << QByteArray("{}");
  QTest::newRow("empty") << QByteArray("[]");
  QTest::newRow("unusable") << QByteArray(R"([null,17,{}, {"en_US":" "}])");
}

void TestGreetingSticker::test_failedCatalogIsCached() {
  QFETCH(QByteArray, catalog);
  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const auto name = QStringLiteral("tips.json");
  const auto path = catalog.isNull() ? tmp.filePath(name) : tmp.createFile(name, catalog);
  QCOMPARE(ToolTipService::randomTip(path), QString());
  ServiceLocator services;
  ClockedGreeting sticker(services);
  sticker.tipSource = [&] { return ToolTipService::randomTip(path); };
  sticker.show();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  tmp.createFile(name, R"([{"en_US":"Repaired"}])");
  sticker.now = at(12, 2);
  sticker.refresh();
  sticker.hide();
  sticker.show();
  QCOMPARE(sticker.displayedText(), normalGreetingForHour(12));
  sticker.now = at(13, 2);
  sticker.refresh();
  QCOMPARE(sticker.displayedText(), QStringLiteral("Repaired"));
}

void TestGreetingSticker::test_literalAndLongText() {
  const auto longest = longestCatalogTip();
  QVERIFY(!longest.isEmpty());
  ServiceLocator services;
  QWidget frame;
  auto *sticker = new ClockedGreeting(services);
  const auto literal = QStringLiteral("Use <b>literal</b> & keep writing");
  sticker->tips = QStringList{literal, longest};
  sticker->now = at(11, 59);
  populateFrame(&frame, sticker);
  frame.setFixedWidth(300);
  frame.show();
  QCOMPARE(sticker->displayedText(), normalGreetingForHour(11));
  sticker->now = at(12);
  sticker->refresh();
  QCOMPARE(sticker->displayedText(), literal);
  QCOMPARE(sticker->body()->textFormat(), Qt::PlainText);
  sticker->now = at(13);
  sticker->refresh();
  for (int width : {300, 140}) {
    frame.setFixedWidth(width);
    QTest::qWait(50);
    // The longest shipped tip may fit at 300px with the native font. It must
    // remain fully laid out at either width and actually scroll at 140px.
    QTRY_VERIFY(sticker->body()->height() >=
                sticker->body()->heightForWidth(sticker->body()->width()));
    if (width == 140) {
      QVERIFY(sticker->area()->verticalScrollBar()->maximum() > 0);
    }
    QCOMPARE(sticker->displayedText(), longest);
    QCOMPARE(frame.size(), QSize(width, 100));
    QVERIFY(!sticker->area()->horizontalScrollBar()->isVisible());
    auto *bar = sticker->area()->verticalScrollBar();
    bar->setValue(bar->maximum());
    QCoreApplication::processEvents();
    const auto bottom =
        sticker->body()->mapTo(sticker->area()->viewport(), sticker->body()->rect().bottomLeft());
    QVERIFY(sticker->area()->viewport()->rect().contains(bottom));
    const int scrolled = bar->value();
    sticker->refresh();
    QCOMPARE(bar->value(), scrolled); // Refreshing the same tip must not reset reading position.
  }
  frame.setFixedWidth(300);
  sticker->now = at(13, 5);
  sticker->refresh();
  QCOMPARE(sticker->displayedText(), normalGreetingForHour(13));
  QTRY_COMPARE(sticker->area()->verticalScrollBar()->maximum(), 0);
}

void TestGreetingSticker::test_boardDoesNotGrow() {
  BoardFixture board;
  QTest::qWait(50);
  const auto longest = longestCatalogTip();
  QVERIFY(!longest.isEmpty());
  const int leftWidth = board.left->width();
  const int rightWidth = board.right->width();
  const int horizontalRange = board.outer.horizontalScrollBar()->maximum();
  QVERIFY(qAbs(leftWidth - rightWidth) <= 1);
  board.sticker->tips = QStringList{longest};
  board.sticker->now = at(12);
  board.sticker->refresh();
  QTRY_COMPARE(board.sticker->displayedText(), longest);
  QTest::qWait(50);
  QCOMPARE(board.left->width(), leftWidth);
  QCOMPARE(board.right->width(), rightWidth);
  QCOMPARE(board.outer.horizontalScrollBar()->maximum(), horizontalRange);
  QCOMPARE(board.left->height(), 100);
  QCOMPARE(board.right->height(), 100);
}

void TestGreetingSticker::test_wheelRouting() {
  BoardFixture board;
  QTest::qWait(50);
  auto *inner = board.sticker->area()->verticalScrollBar();
  auto *outer = board.outer.verticalScrollBar();
  QCOMPARE(inner->maximum(), 0);
  QVERIFY(outer->maximum() > 0);
  wheelDown(board.sticker->area()->viewport());
  QTRY_VERIFY(outer->value() > 0);
  outer->setValue(0);
  const auto longest = longestCatalogTip();
  QVERIFY(!longest.isEmpty());
  // Force overflow independently of whether one catalog entry fits the native font.
  board.sticker->tips = QStringList{longest + QLatin1Char(' ') + longest};
  board.sticker->now = at(12);
  board.sticker->refresh();
  QTRY_VERIFY(inner->maximum() > 0);
  QCOMPARE(inner->value(), 0);
  wheelDown(board.sticker->area()->viewport());
  QTRY_VERIFY(inner->value() > 0);
  QCOMPARE(outer->value(), 0);
}

} // namespace tests

QTEST_MAIN(tests::TestGreetingSticker)
#include "test_greetingsticker.moc"
