// test_hookcontext.cpp - Unit tests for HookContext
#include <QtTest>

#include <core/hookcontext.h>

using namespace vnotex;

namespace tests {

class TestHookContext : public QObject {
  Q_OBJECT

private slots:
  void testDefaultConstruction();
  void testNamedConstruction();
  void testCancel();
  void testCancelIdempotent();
  void testStopPropagation();
  void testStopPropagationIdempotent();
  void testCancelAndStopPropagationIndependent();
  void testSetGetMetadata();
  void testGetMetadataDefault();
  void testMultipleMetadata();
  void testMetadataOverwrite();
  void testMetadataMap();
  void testCopySemantics();
};

void TestHookContext::testDefaultConstruction() {
  HookContext ctx;

  QVERIFY(ctx.hookName().isEmpty());
  QVERIFY(!ctx.isCancelled());
  QVERIFY(!ctx.isPropagationStopped());
  QVERIFY(ctx.metadata().isEmpty());
}

void TestHookContext::testNamedConstruction() {
  HookContext ctx("my.hook");

  QCOMPARE(ctx.hookName(), QString("my.hook"));
}

void TestHookContext::testCancel() {
  HookContext ctx;

  ctx.cancel();

  QVERIFY(ctx.isCancelled());
}

void TestHookContext::testCancelIdempotent() {
  HookContext ctx;

  ctx.cancel();
  ctx.cancel();

  QVERIFY(ctx.isCancelled());
}

void TestHookContext::testStopPropagation() {
  HookContext ctx;

  ctx.stopPropagation();

  QVERIFY(ctx.isPropagationStopped());
}

void TestHookContext::testStopPropagationIdempotent() {
  HookContext ctx;

  ctx.stopPropagation();
  ctx.stopPropagation();

  QVERIFY(ctx.isPropagationStopped());
}

void TestHookContext::testCancelAndStopPropagationIndependent() {
  HookContext cancelledOnly;
  cancelledOnly.cancel();
  QVERIFY(cancelledOnly.isCancelled());
  QVERIFY(!cancelledOnly.isPropagationStopped());

  HookContext stoppedOnly;
  stoppedOnly.stopPropagation();
  QVERIFY(!stoppedOnly.isCancelled());
  QVERIFY(stoppedOnly.isPropagationStopped());
}

void TestHookContext::testSetGetMetadata() {
  HookContext ctx;

  ctx.setMetadata("key", 42);

  QCOMPARE(ctx.getMetadata("key"), QVariant(42));
}

void TestHookContext::testGetMetadataDefault() {
  HookContext ctx;

  QCOMPARE(ctx.getMetadata("missing"), QVariant());
  QCOMPARE(ctx.getMetadata("missing", QString("default")), QVariant(QString("default")));
}

void TestHookContext::testMultipleMetadata() {
  HookContext ctx;

  ctx.setMetadata("int", 1);
  ctx.setMetadata("string", QString("value"));
  ctx.setMetadata("bool", true);

  QCOMPARE(ctx.getMetadata("int"), QVariant(1));
  QCOMPARE(ctx.getMetadata("string"), QVariant(QString("value")));
  QCOMPARE(ctx.getMetadata("bool"), QVariant(true));
}

void TestHookContext::testMetadataOverwrite() {
  HookContext ctx;

  ctx.setMetadata("key", 1);
  ctx.setMetadata("key", 2);

  QCOMPARE(ctx.getMetadata("key"), QVariant(2));
}

void TestHookContext::testMetadataMap() {
  HookContext ctx;

  ctx.setMetadata("a", 10);
  ctx.setMetadata("b", QString("x"));

  QVariantMap expected;
  expected["a"] = 10;
  expected["b"] = QString("x");

  QCOMPARE(ctx.metadata(), expected);
}

void TestHookContext::testCopySemantics() {
  HookContext original("copy.test");
  original.setMetadata("value", 1);

  HookContext copy = original;
  copy.setMetadata("value", 2);
  copy.setMetadata("copyOnly", true);
  copy.cancel();
  copy.stopPropagation();

  QCOMPARE(original.hookName(), QString("copy.test"));
  QCOMPARE(original.getMetadata("value"), QVariant(1));
  QCOMPARE(original.getMetadata("copyOnly"), QVariant());
  QVERIFY(!original.isCancelled());
  QVERIFY(!original.isPropagationStopped());

  QCOMPARE(copy.hookName(), QString("copy.test"));
  QCOMPARE(copy.getMetadata("value"), QVariant(2));
  QCOMPARE(copy.getMetadata("copyOnly"), QVariant(true));
  QVERIFY(copy.isCancelled());
  QVERIFY(copy.isPropagationStopped());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHookContext)
#include "test_hookcontext.moc"
