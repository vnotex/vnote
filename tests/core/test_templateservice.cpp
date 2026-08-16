#include <QtTest>

#include <QDir>
#include <QFile>

#include <core/configmgr2.h>
#include <core/services/configcoreservice.h>
#include <core/services/templateservice.h>
#include <utils/fileutils2.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTemplateService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // templates/ is a bundled extra-data folder, so it carries the install stamp
  // (.vnote-extra-version). The stamp is NOT hidden on Windows, so it must be
  // filtered out explicitly or it shows up as a selectable template.
  void testGetTemplates_excludesTheExtraDataVersionStamp();

private:
  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
};

void TestTemplateService::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configService = new ConfigCoreService(m_context);
  m_configMgr = new ConfigMgr2(m_configService);
  m_configMgr->init();
}

void TestTemplateService::cleanupTestCase() {
  delete m_configMgr;
  delete m_configService;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestTemplateService::testGetTemplates_excludesTheExtraDataVersionStamp() {
  TemplateService service(m_configMgr);
  QVERIFY(service.ensureTemplateFolder());

  const QDir dir(service.getTemplateFolder());
  const QString stamp = dir.filePath(QLatin1String(FileUtils2::c_versionStampFileName));
  {
    QFile file(stamp);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("4.4.4") > 0);
  }

  const QString real = dir.filePath(QStringLiteral("title.md"));
  {
    QFile file(real);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("# %no%\n") > 0);
  }

  const QStringList templates = service.getTemplates();
  QVERIFY2(templates.contains(QStringLiteral("title.md")),
           "a real template disappeared from the listing");
  QVERIFY2(!templates.contains(QLatin1String(FileUtils2::c_versionStampFileName)),
           "the extra-data version stamp is offered as a template");

  QFile::remove(stamp);
  QFile::remove(real);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTemplateService)
#include "test_templateservice.moc"
