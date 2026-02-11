// test_servicelocator.cpp - Tests for vnotex::ServiceLocator
#include <QtTest>

#include <core/servicelocator.h>

using namespace vnotex;

namespace tests {

// Mock service for testing
class MockService {
public:
  MockService(int value) : m_value(value) {}
  int getValue() const { return m_value; }

private:
  int m_value;
};

class AnotherService {
public:
  QString getName() const { return "AnotherService"; }
};

class TestServiceLocator : public QObject {
  Q_OBJECT

private slots:
  // Basic registration and retrieval
  void testRegisterAndGet();

  // Get unregistered service
  void testGetUnregistered();

  // Check service existence
  void testHas();

  // Multiple services
  void testMultipleServices();

  // Overwrite registration
  void testOverwriteRegistration();
};

void TestServiceLocator::testRegisterAndGet() {
  ServiceLocator locator;
  MockService service(42);

  locator.registerService<MockService>(&service);

  MockService *retrieved = locator.get<MockService>();
  QVERIFY(retrieved != nullptr);
  QCOMPARE(retrieved, &service);
  QCOMPARE(retrieved->getValue(), 42);
}

void TestServiceLocator::testGetUnregistered() {
  ServiceLocator locator;

  MockService *retrieved = locator.get<MockService>();
  QVERIFY(retrieved == nullptr);
}

void TestServiceLocator::testHas() {
  ServiceLocator locator;
  MockService service(10);

  QVERIFY(!locator.has<MockService>());

  locator.registerService<MockService>(&service);

  QVERIFY(locator.has<MockService>());
  QVERIFY(!locator.has<AnotherService>());
}

void TestServiceLocator::testMultipleServices() {
  ServiceLocator locator;
  MockService mockService(100);
  AnotherService anotherService;

  locator.registerService<MockService>(&mockService);
  locator.registerService<AnotherService>(&anotherService);

  QVERIFY(locator.has<MockService>());
  QVERIFY(locator.has<AnotherService>());

  MockService *mock = locator.get<MockService>();
  AnotherService *another = locator.get<AnotherService>();

  QVERIFY(mock != nullptr);
  QVERIFY(another != nullptr);
  QCOMPARE(mock->getValue(), 100);
  QCOMPARE(another->getName(), QString("AnotherService"));
}

void TestServiceLocator::testOverwriteRegistration() {
  ServiceLocator locator;
  MockService service1(1);
  MockService service2(2);

  locator.registerService<MockService>(&service1);
  MockService *retrieved1 = locator.get<MockService>();
  QCOMPARE(retrieved1->getValue(), 1);

  // Overwrite with service2
  locator.registerService<MockService>(&service2);
  MockService *retrieved2 = locator.get<MockService>();
  QCOMPARE(retrieved2->getValue(), 2);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestServiceLocator)
#include "test_servicelocator.moc"
