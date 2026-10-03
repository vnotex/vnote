// Persistence and layout regressions on real settings pages:
//   - QuickAccessPage: emptying the quick-access box was skipped by an
//     `if (!text.isEmpty())` guard, so the old list survived the restart.
//   - FileAssociationPage: reloading and adding programs must keep their inputs
//     above the compact Add Program button, including when it is nested in a row.
//
// The pages are constructed for real (not stubbed as in test_settings_slug),
// so this is NOT GUILESS: it needs a QApplication.

#include <QCheckBox>
#include <QtTest>

#include <QJsonArray>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/templateservice.h>
#include <core/sessionconfig.h>
#include <widgets/dialogs/settings/fileassociationpage.h>
#include <widgets/dialogs/settings/generalpage.h>
#include <widgets/dialogs/settings/mindmapeditorpage.h>
#include <widgets/dialogs/settings/notemanagementpage.h>
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
  void test_generalPageLoadsAppNameAndRequiresRestartWhenEdited();

  void test_noteManagementLoadsRecycleBinCleanupSettings();
  void test_noteManagementPersistsRecycleBinCleanupSettings();
  void test_mindMapPreferencesPersistAndReset();

private:
  void seedQuickAccess(const QStringList &p_paths);

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;
  FileTypeCoreService *m_fileTypeService = nullptr;

  ServiceLocator *m_services = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  TemplateService *m_templateService = nullptr;
  HookManager *m_hooks = nullptr;
  QJsonObject m_originalMindMapConfig;
};

void TestSettingsPersistence::initTestCase() {
  // CRITICAL: before any vxcore context is created.
  vxcore_set_test_mode(1);

  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_configService = new ConfigCoreService(m_context);
  m_fileTypeService = new FileTypeCoreService(m_context, QStringLiteral("en_US"));
  m_hooks = new HookManager();
}

void TestSettingsPersistence::cleanupTestCase() {
  // EditorPage's shared single-shot timer captures this suite-owned service.
  QTest::qWait(1100);
  delete m_hooks;
  m_hooks = nullptr;
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
  m_services->registerService<HookManager>(m_hooks);

  m_configMgr = new ConfigMgr2(m_configService);
  m_configMgr->init();
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_originalMindMapConfig = m_configMgr->getEditorConfig().getMindMapEditorConfig().toJson();

  // QuickAccessPage embeds a NoteTemplateSelector, which dereferences this
  // service unconditionally while building its combo box.
  m_templateService = new TemplateService(m_configMgr);
  m_services->registerService<TemplateService>(m_templateService);
}

void TestSettingsPersistence::cleanup() {
  m_configMgr->getEditorConfig().getMindMapEditorConfig().fromJson(m_originalMindMapConfig);
  m_configMgr->updateMainConfig(m_configMgr->getConfig().toJson());
  m_configMgr->getCoreConfig().setAppName(QStringLiteral("VNote"));
  m_configMgr->getCoreConfig().setRecycleBinAutoCleanupEnabled(false, 0);
  m_configMgr->getCoreConfig().setRecycleBinRetentionDays(60);
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

  page.resize(1000, 900);
  page.show();

  for (int reload = 0; reload < 2; ++reload) {
    page.load();

    QPushButton *addButton = nullptr;
    for (auto *button : page.findChildren<QPushButton *>()) {
      if (button->text() == QStringLiteral("Add Program")) {
        QVERIFY2(!addButton, "more than one Add Program button survived the reload");
        addButton = button;
      }
    }
    QVERIFY(addButton);
    QTRY_VERIFY(addButton->isVisible());
    QCOMPARE(page.findChildren<QLineEdit *>(QStringLiteral("nameEdit")).size(), 1);

    QTest::mouseClick(addButton, Qt::LeftButton);
    QTRY_COMPARE(page.findChildren<QLineEdit *>(QStringLiteral("nameEdit")).size(), 2);
    QTRY_COMPARE(addButton->width(), addButton->sizeHint().width());

    for (auto *edit : page.findChildren<QLineEdit *>(QStringLiteral("suffixesEdit"))) {
      QTRY_VERIFY(edit->mapTo(&page, QPoint(0, edit->height())).y() <=
                  addButton->mapTo(&page, QPoint()).y());
    }
  }
}

void TestSettingsPersistence::test_generalPageLoadsAppNameAndRequiresRestartWhenEdited() {
  m_configMgr->getCoreConfig().setAppName(QStringLiteral("My Notes"));

  GeneralPage page(*m_services);
  page.load();

  auto *edit = page.findChild<QLineEdit *>(QStringLiteral("applicationDisplayNameLineEdit"));
  QVERIFY(edit);
  QCOMPARE(edit->text(), QStringLiteral("My Notes"));
  QVERIFY(!page.isRestartNeeded());

  edit->setText(QStringLiteral("Work Notes"));
  QVERIFY(page.isRestartNeeded());
}

void TestSettingsPersistence::test_noteManagementLoadsRecycleBinCleanupSettings() {
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinAutoCleanupEnabled(false, 0);
  config.setRecycleBinRetentionDays(60);

  NoteManagementPage page(*m_services);
  page.load();
  auto *enabled = page.findChild<QCheckBox *>(QStringLiteral("RecycleBinAutoCleanupCheckBox"));
  auto *retention = page.findChild<QSpinBox *>(QStringLiteral("RecycleBinRetentionDaysSpinBox"));
  QVERIFY(enabled);
  QVERIFY(retention);
  QVERIFY(!enabled->isChecked());
  QVERIFY(!retention->isEnabled());
  QCOMPARE(retention->value(), 60);

  config.setRecycleBinRetentionDays(45);
  config.setRecycleBinAutoCleanupEnabled(true, Q_INT64_C(1785337074532));
  page.load();
  QVERIFY(enabled->isChecked());
  QVERIFY(retention->isEnabled());
  QCOMPARE(retention->value(), 45);

  enabled->setChecked(false);
  QVERIFY(!retention->isEnabled());
  enabled->setChecked(true);
  QVERIFY(retention->isEnabled());
}

void TestSettingsPersistence::test_noteManagementPersistsRecycleBinCleanupSettings() {
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinAutoCleanupEnabled(false, 0);
  config.setRecycleBinRetentionDays(60);

  NoteManagementPage page(*m_services);
  page.load();
  auto *enabled = page.findChild<QCheckBox *>(QStringLiteral("RecycleBinAutoCleanupCheckBox"));
  auto *retention = page.findChild<QSpinBox *>(QStringLiteral("RecycleBinRetentionDaysSpinBox"));
  QVERIFY(enabled);
  QVERIFY(retention);
  enabled->setChecked(true);
  retention->setValue(90);
  QVERIFY(page.save());
  QVERIFY(config.isRecycleBinAutoCleanupEnabled());
  QCOMPARE(config.getRecycleBinRetentionDays(), 90);
  QVERIFY(config.getRecycleBinCleanupEnabledSinceUtc() > 0);

  NoteManagementPage reloaded(*m_services);
  reloaded.load();
  auto *reloadedEnabled =
      reloaded.findChild<QCheckBox *>(QStringLiteral("RecycleBinAutoCleanupCheckBox"));
  auto *reloadedRetention =
      reloaded.findChild<QSpinBox *>(QStringLiteral("RecycleBinRetentionDaysSpinBox"));
  QVERIFY(reloadedEnabled);
  QVERIFY(reloadedRetention);
  QVERIFY(reloadedEnabled->isChecked());
  QVERIFY(reloadedRetention->isEnabled());
  QCOMPARE(reloadedRetention->value(), 90);
}

void TestSettingsPersistence::test_mindMapPreferencesPersistAndReset() {
  auto &config = m_configMgr->getEditorConfig().getMindMapEditorConfig();
  const QJsonObject shortcuts{{QStringLiteral("addChild"), QJsonArray{QStringLiteral("Ctrl+J")}},
                              {QStringLiteral("toggleBold"), QJsonArray{}},
                              {QStringLiteral("unknown"), QJsonValue::Null}};
  config.fromJson(QJsonObject{{QStringLiteral("shortcuts"), shortcuts}});
  MindMapEditorPage page(*m_services);
  page.load();
  auto *family = page.findChild<QLineEdit *>(QStringLiteral("mindMapFontFamily"));
  auto *size = page.findChild<QSpinBox *>(QStringLiteral("mindMapFontSize"));
  auto *limit = page.findChild<QSpinBox *>(QStringLiteral("mindMapUndoLimit"));
  auto *confirm = page.findChild<QCheckBox *>(QStringLiteral("mindMapConfirmSubtreeDeletion"));
  auto *colors = page.findChild<QCheckBox *>(QStringLiteral("mindMapAutoRandomBranchColor"));
  QVERIFY(family && size && limit && confirm && colors);
  const auto savedDefaults = config.toJson();
  family->setText(QStringLiteral("  Missing Font, Noto Sans  "));
  size->setValue(18);
  limit->setValue(2);
  confirm->setChecked(false);
  colors->setChecked(false);
  QCOMPARE(config.toJson(), savedDefaults);
  QVERIFY(!page.isRestartNeeded());
  QVERIFY(page.save());
  QCOMPARE(config.getFontFamily(), QStringLiteral("Missing Font, Noto Sans"));
  QCOMPARE(config.getFontPointSize(), 18);
  QCOMPARE(config.getUndoLimit(), 2);
  QVERIFY(!config.getConfirmSubtreeDeletion() && !config.getAutoRandomBranchColor());
  QCOMPARE(config.toJson().value(QStringLiteral("shortcuts")).toObject(), shortcuts);
  for (const auto &query : {QStringLiteral("font"), QStringLiteral("undo"),
                            QStringLiteral("branch"), QStringLiteral("shortcuts")}) {
    QVERIFY(page.search(query));
  }
  const auto persisted = [&] {
    return m_configService->getConfigByName(DataLocation::App, QStringLiteral("vnotex"))
        .value(QStringLiteral("editor"))
        .toObject()
        .value(QStringLiteral("mindMapEditor"))
        .toObject();
  };
  QTRY_COMPARE(persisted(), config.toJson());
  {
    ConfigMgr2 freshManager(m_configService);
    freshManager.init();
    QCOMPARE(freshManager.getEditorConfig().getMindMapEditorConfig().toJson(), config.toJson());
    m_services->registerService<ConfigMgr2>(&freshManager);
    MindMapEditorPage freshPage(*m_services);
    freshPage.load();
    QCOMPARE(freshPage.findChild<QLineEdit *>(QStringLiteral("mindMapFontFamily"))->text(),
             config.getFontFamily());
    QCOMPARE(freshPage.findChild<QSpinBox *>(QStringLiteral("mindMapFontSize"))->value(), 18);
    QCOMPARE(freshPage.findChild<QSpinBox *>(QStringLiteral("mindMapUndoLimit"))->value(), 2);
    QVERIFY(!freshPage.findChild<QCheckBox *>(QStringLiteral("mindMapConfirmSubtreeDeletion"))
                 ->isChecked());
    QVERIFY(!freshPage.findChild<QCheckBox *>(QStringLiteral("mindMapAutoRandomBranchColor"))
                 ->isChecked());
    m_services->registerService<ConfigMgr2>(m_configMgr);
  }
  family->setText(QStringLiteral("Discarded"));
  size->setValue(40);
  limit->setValue(0);
  confirm->setChecked(true);
  colors->setChecked(true);
  page.reset();
  QCOMPARE(family->text(), QStringLiteral("Missing Font, Noto Sans"));
  QCOMPARE(size->value(), 18);
  QCOMPARE(limit->value(), 2);
  QVERIFY(!confirm->isChecked() && !colors->isChecked());
  family->clear();
  size->setValue(0);
  QVERIFY(page.save());
  QVERIFY(config.getFontFamily().isEmpty());
  QCOMPARE(config.getFontPointSize(), 0);
  QCOMPARE(config.getUndoLimit(), 2);
  QVERIFY(!config.getConfirmSubtreeDeletion() && !config.getAutoRandomBranchColor());
  QCOMPARE(config.toJson().value(QStringLiteral("shortcuts")).toObject(), shortcuts);
  QTRY_COMPARE(persisted(), config.toJson());
}

} // namespace tests

QTEST_MAIN(tests::TestSettingsPersistence)
#include "test_settings_persistence.moc"
