#include <QtTest>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointF>
#include <QRegularExpression>
#include <QShortcut>
#include <QWidget>

#include <core/coreconfig.h>
#include <gui/utils/widgetutils.h>
#include <gui/services/navigationmodeservice.h>
#include <widgets/navigationmode.h>

namespace vnotex {

NavigationMode::NavigationMode(Type p_type, QWidget *p_widget, ThemeService *p_themeService)
    : m_type(p_type), m_widget(p_widget), m_themeService(p_themeService) {}

void NavigationMode::registerNavigation(QChar p_majorKey) { m_majorKey = p_majorKey; }

void NavigationMode::showNavigation() {}

void NavigationMode::hideNavigation() {}

NavigationMode::Status NavigationMode::handleKeyNavigation(int) { return Status(); }

bool NavigationMode::isTargetVisible() { return true; }

QVector<void *> NavigationMode::getVisibleNavigationItems() { return QVector<void *>(); }

void NavigationMode::showNavigationWithDoubleKeys() {}

void NavigationMode::clearNavigation() {}

QShortcut *WidgetUtils::createShortcut(const QString &p_shortcut, QWidget *p_widget,
                                       Qt::ShortcutContext p_context) {
  QKeySequence kseq(p_shortcut);
  if (kseq.isEmpty()) {
    return nullptr;
  }

  return new QShortcut(kseq, p_widget, nullptr, nullptr, p_context);
}

bool WidgetUtils::isMetaKey(int p_key) {
  return p_key == Qt::Key_Control || p_key == Qt::Key_Shift || p_key == Qt::Key_Meta ||
         p_key == Qt::Key_Alt;
}

} // namespace vnotex

namespace tests {

class MockNavigationMode : public vnotex::NavigationMode {
public:
  explicit MockNavigationMode(QWidget *p_widget)
      : vnotex::NavigationMode(vnotex::NavigationMode::Type::DoubleKeys, p_widget, nullptr) {}

  int showCount = 0;
  int hideCount = 0;
  int handleKeyCount = 0;
  int lastKey = 0;
  bool consumeKey = false;
  bool hitTarget = false;

  void showNavigation() override { ++showCount; }

  void hideNavigation() override { ++hideCount; }

  vnotex::NavigationMode::Status handleKeyNavigation(int p_key) override {
    ++handleKeyCount;
    lastKey = p_key;

    Status sta;
    sta.m_isKeyConsumed = consumeKey;
    sta.m_isTargetHit = hitTarget;
    return sta;
  }

protected:
  void handleTargetHit(void *) override {}

  void placeNavigationLabel(int, void *, QLabel *) override {}

  QVector<void *> getVisibleNavigationItems() override { return {}; }
};

class TestNavigationModeService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void testServiceConstruction();
  void testRegisterSingleTarget();
  void testRegisterMultipleTargets();
  void testRegisterMaxTargets();
  void testTriggerNavigationMode();
  void testExitNavigationModeByKey();
  void testKeyDispatchConsumes();
  void testKeyDispatchHidesNonMatching();
  void testMouseClickExits();
  void testMetaKeyIgnored();
  void testNoTargetsWarning();
  void testUnregisterTarget();
  void testUnregisterDuringActiveMode();

private:
  vnotex::CoreConfig *m_coreConfig = nullptr;
  QWidget *m_topLevelWidget = nullptr;
  vnotex::NavigationModeService *m_service = nullptr;
};

void TestNavigationModeService::initTestCase() {
  m_coreConfig = new vnotex::CoreConfig(nullptr, nullptr);

  m_topLevelWidget = new QWidget();
  m_topLevelWidget->resize(400, 300);
  m_topLevelWidget->show();
  QCoreApplication::processEvents();
}

void TestNavigationModeService::cleanupTestCase() {
  delete m_topLevelWidget;
  m_topLevelWidget = nullptr;

  delete m_coreConfig;
  m_coreConfig = nullptr;
}

void TestNavigationModeService::init() {
  m_service = new vnotex::NavigationModeService(*m_coreConfig, m_topLevelWidget, this);
}

void TestNavigationModeService::cleanup() {
  delete m_service;
  m_service = nullptr;
}

void TestNavigationModeService::testServiceConstruction() { QVERIFY(m_service != nullptr); }

void TestNavigationModeService::testRegisterSingleTarget() {
  MockNavigationMode target(m_topLevelWidget);
  m_service->registerNavigationTarget(&target);
  QVERIFY(true);
}

void TestNavigationModeService::testRegisterMultipleTargets() {
  MockNavigationMode t1(m_topLevelWidget);
  MockNavigationMode t2(m_topLevelWidget);
  MockNavigationMode t3(m_topLevelWidget);
  m_service->registerNavigationTarget(&t1);
  m_service->registerNavigationTarget(&t2);
  m_service->registerNavigationTarget(&t3);
  QVERIFY(true);
}

void TestNavigationModeService::testRegisterMaxTargets() {
  QVector<MockNavigationMode *> targets;
  for (int i = 0; i < 27; ++i) {
    targets.append(new MockNavigationMode(m_topLevelWidget));
  }

  for (int i = 0; i < 26; ++i) {
    m_service->registerNavigationTarget(targets[i]);
  }

  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression("failed to register navigation target.*"));
  m_service->registerNavigationTarget(targets[26]);

  qDeleteAll(targets);
}

void TestNavigationModeService::testTriggerNavigationMode() {
  MockNavigationMode target(m_topLevelWidget);
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
  QCOMPARE(target.showCount, 1);

  m_service->unregisterNavigationTarget(&target);
}

void TestNavigationModeService::testExitNavigationModeByKey() {
  MockNavigationMode target(m_topLevelWidget);
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
  QCOMPARE(target.showCount, 1);

  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
  QCoreApplication::sendEvent(m_topLevelWidget, &keyEvent);

  QVERIFY(target.hideCount >= 1);
}

void TestNavigationModeService::testKeyDispatchConsumes() {
  MockNavigationMode target(m_topLevelWidget);
  target.consumeKey = true;
  target.hitTarget = true;
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));

  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
  QCoreApplication::sendEvent(m_topLevelWidget, &keyEvent);

  QVERIFY(target.handleKeyCount >= 1);
  QCOMPARE(target.lastKey, int(Qt::Key_A));
}

void TestNavigationModeService::testKeyDispatchHidesNonMatching() {
  MockNavigationMode t1(m_topLevelWidget);
  MockNavigationMode t2(m_topLevelWidget);
  t2.consumeKey = true;
  t2.hitTarget = true;

  m_service->registerNavigationTarget(&t1);
  m_service->registerNavigationTarget(&t2);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));

  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
  QCoreApplication::sendEvent(m_topLevelWidget, &keyEvent);

  QVERIFY(t1.hideCount >= 1);
  QVERIFY(t2.handleKeyCount >= 1);
}

void TestNavigationModeService::testMouseClickExits() {
  MockNavigationMode target(m_topLevelWidget);
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
  QCOMPARE(target.showCount, 1);

  QMouseEvent mouseEvent(QEvent::MouseButtonPress,
                         QPointF(10, 10),
                         QPointF(10, 10),
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
  QCoreApplication::sendEvent(m_topLevelWidget, &mouseEvent);

  QVERIFY(target.hideCount >= 1);
}

void TestNavigationModeService::testMetaKeyIgnored() {
  MockNavigationMode target(m_topLevelWidget);
  target.consumeKey = true;
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));

  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
  QCoreApplication::sendEvent(m_topLevelWidget, &keyEvent);

  QCOMPARE(target.handleKeyCount, 0);
}

void TestNavigationModeService::testNoTargetsWarning() {
  QTest::ignoreMessage(QtInfoMsg, "no navigation target");
  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
}

void TestNavigationModeService::testUnregisterTarget() {
  MockNavigationMode t1(m_topLevelWidget);
  MockNavigationMode t2(m_topLevelWidget);
  m_service->registerNavigationTarget(&t1);
  m_service->registerNavigationTarget(&t2);

  m_service->unregisterNavigationTarget(&t1);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
  QCOMPARE(t1.showCount, 0);
  QCOMPARE(t2.showCount, 1);

  m_service->unregisterNavigationTarget(&t2);
}

void TestNavigationModeService::testUnregisterDuringActiveMode() {
  MockNavigationMode target(m_topLevelWidget);
  m_service->registerNavigationTarget(&target);

  QVERIFY(QMetaObject::invokeMethod(m_service, "triggerNavigationMode", Qt::DirectConnection));
  QCOMPARE(target.showCount, 1);

  m_service->unregisterNavigationTarget(&target);

  QVERIFY(target.hideCount >= 1);
}

} // namespace tests

QTEST_MAIN(tests::TestNavigationModeService)
#include "test_navigationmodeservice.moc"
