// Persistence regressions on two real settings pages.
//
// Both bugs let a user edit vanish silently, which is exactly the class of
// defect that also hit ImageHostPage:
//   - QuickAccessPage: emptying the quick-access box was skipped by an
//     `if (!text.isEmpty())` guard, so the old list survived the restart.
//   - FileAssociationPage: a reload deleted the "Add Program" button but left
//     m_addProgramButton dangling, and the row loop below dereferenced it
//     before it was re-created, so rows landed AFTER the button.
//
// The pages are constructed for real (not stubbed as in test_settings_slug),
// so this is NOT GUILESS: it needs a QApplication.

#include <QtTest>

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/templateservice.h>
#include <core/sessionconfig.h>
#include <widgets/dialogs/settings/fileassociationpage.h>
#include <widgets/dialogs/settings/quickaccesspage.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestSettingsPersistence : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void test_clearingQuickAccessPersistsAnEmptyList();
  void test_reloadClearsTheBoxWhenTheConfigIsEmpty();
  void test_quickAccessRoundTripsItsItems();
  void test_reloadKeepsProgramRowsAboveTheAddButton();

private:
  void seedQuickAccess(const QStringList &p_paths);

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  FileTypeCoreService *m_fileTypeService = nullptr;

  ServiceLocator *m_services = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  TemplateService *m_templateService = nullptr;
};

void TestSettingsPersistence::initTestCase() {
  // CRITICAL: before any vxcore context is created.
  vxcore_set_test_mode(1);

  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_configService = new ConfigCoreService(m_context);
  m_fileTypeService = new FileTypeCoreService(m_context, QStringLiteral("en_US"));
}

void TestSettingsPersistence::cleanupTestCase() {
  delete m_fileTypeService;
  m_fileTypeService = nullptr;
  delete m_configService;
  m_configService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSettingsPersistence::init() {
  m_services = new ServiceLocator();
  m_services->registerService<ConfigCoreService>(m_configService);
  m_services->registerService<FileTypeCoreService>(m_fileTypeService);

  m_configMgr = new ConfigMgr2(m_configService);
  m_services->registerService<ConfigMgr2>(m_configMgr);

  // QuickAccessPage embeds a NoteTemplateSelector, which dereferences this
  // service unconditionally while building its combo box.
  m_templateService = new TemplateService(m_configMgr);
  m_services->registerService<TemplateService>(m_templateService);
}

void TestSettingsPersistence::cleanup() {
  m_configMgr->getSessionConfig().setQuickAccessItems({});
  m_configMgr->getSessionConfig().setExternalPrograms({});

  // Destroy the locator first: it holds non-owning pointers, and its documented
  // contract is that registered services outlive it. The two core services are
  // owned by the fixture and outlive every case.
  delete m_services;
  m_services = nullptr;
  delete m_templateService;
  m_templateService = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
}

void TestSettingsPersistence::seedQuickAccess(const QStringList &p_paths) {
  QVector<SessionConfig::QuickAccessItem> items;
  for (const auto &path : p_paths) {
    SessionConfig::QuickAccessItem item;
    item.m_path = path;
    item.m_openMode = QuickAccessOpenMode::Default;
    items.append(item);
  }
  m_configMgr->getSessionConfig().setQuickAccessItems(items);
}

void TestSettingsPersistence::test_clearingQuickAccessPersistsAnEmptyList() {
  seedQuickAccess({QStringLiteral("/tmp/a.md"), QStringLiteral("/tmp/b.md")});

  QuickAccessPage page(*m_services);
  page.load();

  auto *edit = page.findChild<QPlainTextEdit *>();
  QVERIFY(edit);
  QVERIFY(!edit->toPlainText().isEmpty());

  // The user selects everything and deletes it. textChanged marks the page
  // dirty, so save() actually runs saveInternal().
  edit->setPlainText(QString());
  QVERIFY(page.save());

  QVERIFY(m_configMgr->getSessionConfig().getQuickAccessItems().isEmpty());

  // A reload must not resurrect the old text either.
  page.load();
  QVERIFY(edit->toPlainText().isEmpty());
}

void TestSettingsPersistence::test_reloadClearsTheBoxWhenTheConfigIsEmpty() {
  // Pins the LOAD side on its own: the box holds text, the stored list becomes
  // empty behind its back (another window, or Reset after a save), and the
  // reload must clear it. The clearing case above cannot catch a regression
  // here, because there the box is already empty when load() runs.
  seedQuickAccess({QStringLiteral("/tmp/a.md")});

  QuickAccessPage page(*m_services);
  page.load();

  auto *edit = page.findChild<QPlainTextEdit *>();
  QVERIFY(edit);
  QVERIFY(!edit->toPlainText().isEmpty());

  m_configMgr->getSessionConfig().setQuickAccessItems({});
  page.load();
  QVERIFY(edit->toPlainText().isEmpty());
}

void TestSettingsPersistence::test_quickAccessRoundTripsItsItems() {
  seedQuickAccess({QStringLiteral("/tmp/a.md")});

  QuickAccessPage page(*m_services);
  page.load();

  auto *edit = page.findChild<QPlainTextEdit *>();
  QVERIFY(edit);
  edit->setPlainText(QStringLiteral("/tmp/a.md\n/tmp/c.md"));
  QVERIFY(page.save());

  const auto items = m_configMgr->getSessionConfig().getQuickAccessItems();
  QCOMPARE(items.size(), 2);
  QCOMPARE(items[0].m_path, QStringLiteral("/tmp/a.md"));
  QCOMPARE(items[1].m_path, QStringLiteral("/tmp/c.md"));
}

void TestSettingsPersistence::test_reloadKeepsProgramRowsAboveTheAddButton() {
  {
    QVector<SessionConfig::ExternalProgram> programs;
    SessionConfig::ExternalProgram prog;
    prog.m_name = QStringLiteral("viewer");
    prog.m_command = QStringLiteral("viewer %1");
    prog.m_suffixes = QStringList() << QStringLiteral("pdf");
    programs.append(prog);
    m_configMgr->getSessionConfig().setExternalPrograms(programs);
  }

  FileAssociationPage page(*m_services);

  // A second load is where the Add Program button is destroyed and re-created.
  // This is a structural invariant / smoke test of that rebuild, NOT direct
  // coverage of the stale-member bug it accompanies: QLayout::indexOf() only
  // compares pointer identity, so the reverted code still produces this same
  // layout and not even a sanitizer is guaranteed to flag it.
  page.load();
  page.load();

  QPushButton *addButton = nullptr;
  for (auto *btn : page.findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Add Program")) {
      QVERIFY2(!addButton, "more than one Add Program button survived the reload");
      addButton = btn;
    }
  }
  QVERIFY(addButton);

  auto *layout = qobject_cast<QVBoxLayout *>(addButton->parentWidget()->layout());
  QVERIFY(layout);
  const int addIndex = layout->indexOf(addButton);
  QVERIFY(addIndex >= 0);

  // Exactly one external program row (the system-default row is added by
  // SessionConfig itself and carries the same property), and every row sits
  // above the Add Program button.
  int rowCount = 0;
  for (int i = 0; i < layout->count(); ++i) {
    auto *w = layout->itemAt(i)->widget();
    if (w && w->property("programRow").toBool()) {
      QVERIFY2(i < addIndex, "a program row was inserted after the Add Program button");
      if (!w->property("systemRow").toBool()) {
        ++rowCount;
      }
    }
  }
  QCOMPARE(rowCount, 1);
}

} // namespace tests

QTEST_MAIN(tests::TestSettingsPersistence)
#include "test_settings_persistence.moc"
