#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

namespace tests {

class TestImageHostUploadHook : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testRoundTrip();
  void testTypedDoAction();

private:
  HookManager *m_hookMgr = nullptr;
};

void TestImageHostUploadHook::initTestCase() {
  m_hookMgr = new HookManager(this);
  QVERIFY(m_hookMgr != nullptr);
}

void TestImageHostUploadHook::cleanupTestCase() {
  delete m_hookMgr;
  m_hookMgr = nullptr;
}

void TestImageHostUploadHook::testRoundTrip() {
  ImageHostUploadEvent orig;
  orig.providerName = QStringLiteral("myGitHub");
  orig.providerTypeId = QStringLiteral("github");
  orig.fileName = QStringLiteral("screenshot.png");
  orig.destPath = QStringLiteral("images/2024/screenshot.png");
  orig.resultUrl = QStringLiteral("https://raw.githubusercontent.com/user/repo/main/screenshot.png");
  orig.success = true;

  QVariantMap map = orig.toVariantMap();
  ImageHostUploadEvent restored = ImageHostUploadEvent::fromVariantMap(map);

  QCOMPARE(restored.providerName, orig.providerName);
  QCOMPARE(restored.providerTypeId, orig.providerTypeId);
  QCOMPARE(restored.fileName, orig.fileName);
  QCOMPARE(restored.destPath, orig.destPath);
  QCOMPARE(restored.resultUrl, orig.resultUrl);
  QCOMPARE(restored.success, orig.success);
}

void TestImageHostUploadHook::testTypedDoAction() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::ImageHostBeforeUpload,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  ImageHostUploadEvent event;
  event.providerName = QStringLiteral("myGitee");
  event.providerTypeId = QStringLiteral("gitee");
  event.fileName = QStringLiteral("photo.jpg");
  event.destPath = QStringLiteral("uploads/photo.jpg");

  m_hookMgr->doAction(HookNames::ImageHostBeforeUpload, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("providerName")].toString(), QStringLiteral("myGitee"));
  QCOMPARE(captured[QStringLiteral("providerTypeId")].toString(), QStringLiteral("gitee"));
  QCOMPARE(captured[QStringLiteral("fileName")].toString(), QStringLiteral("photo.jpg"));
  QCOMPARE(captured[QStringLiteral("destPath")].toString(), QStringLiteral("uploads/photo.jpg"));

  m_hookMgr->removeAction(hookId);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestImageHostUploadHook)
#include "test_imagehostuploadhook.moc"
