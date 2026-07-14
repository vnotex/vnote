// GUI test for HistorySticker (dashboard sticker listing recently opened files).
// NOT GUILESS: constructs a real QWidget (QListView + FileNodeDelegate), so it
// needs QApplication. The sticker is shown but the event loop is never spun
// while visible, so the delegate paint path (which would dereference an
// unregistered ThemeService) is never reached — mirroring the reload_expansion
// test's rationale for omitting ThemeService.

#include <QtTest>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QListView>
#include <QShowEvent>
#include <QToolButton>
#include <QVariantMap>

#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/buffercoreservice.h>
#include <core/services/bufferservice.h>
#include <core/services/historyservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <gui/services/stickerfactory.h>
#include <models/inodelistmodel.h>
#include <temp_dir_fixture.h>
#include <widgets/dashboard/historysticker.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestHistorySticker : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void test_factoryRegistersHistory();
  void test_firstShowLoadsRecent();
  void test_calendarHookSwitchesToDate();
  void test_invalidPayloadPreservesMode();
  void test_secondShowRefreshesActiveMode();
  void test_destructorUnregistersCallback();
  void test_activationOpensExpectedNode();
  void test_clearFilterReturnsToRecent();

private:
  QString createTestNotebook(const QString &p_name);
  void openFile(const QString &p_nbId, const QString &p_name);
  QListView *listViewOf(HistorySticker *p_sticker) const;
  // Drive the protected showEvent without a native window / paint (keeps the
  // delegate's ThemeService-dependent paint path off, since it is unregistered).
  void sendShow(HistorySticker *p_sticker) const;

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferCoreService *m_bufferCore = nullptr;
  HookManager *m_hookManager = nullptr;
  HistoryService *m_historyService = nullptr;
  BufferService *m_bufferService = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tempDir = nullptr;
  QString m_nbId;
};

void TestHistorySticker::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context);
  m_bufferCore = new BufferCoreService(m_context);
  m_hookManager = new HookManager();
  m_historyService = new HistoryService(m_notebookService);
  m_bufferService =
      new BufferService(m_context, m_hookManager, AutoSavePolicy::None);

  m_services = new ServiceLocator();
  m_services->registerService<NotebookCoreService>(m_notebookService);
  m_services->registerService<BufferCoreService>(m_bufferCore);
  m_services->registerService<HookManager>(m_hookManager);
  m_services->registerService<HistoryService>(m_historyService);
  m_services->registerService<BufferService>(m_bufferService);
}

void TestHistorySticker::cleanupTestCase() {
  delete m_services;
  m_services = nullptr;
  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_historyService;
  m_historyService = nullptr;
  delete m_hookManager;
  m_hookManager = nullptr;
  delete m_bufferCore;
  m_bufferCore = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestHistorySticker::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  m_nbId = createTestNotebook(QStringLiteral("hist_nb"));
  QVERIFY(!m_nbId.isEmpty());
}

void TestHistorySticker::cleanup() {
  // Close all buffers and notebooks so history does not leak between tests.
  const QJsonArray buffers = m_bufferCore->listBuffers();
  for (const auto &bufVal : buffers) {
    const QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferCore->closeBuffer(id);
    }
  }
  const QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &nbVal : notebooks) {
    const QString id = nbVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_notebookService->closeNotebook(id);
    }
  }

  delete m_tempDir;
  m_tempDir = nullptr;
}

QString TestHistorySticker::createTestNotebook(const QString &p_name) {
  QString nbPath = m_tempDir->filePath(p_name);
  QString configJson =
      QStringLiteral(R"({"name": "%1", "description": "Test", "version": "1"})").arg(p_name);
  return m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
}

void TestHistorySticker::openFile(const QString &p_nbId, const QString &p_name) {
  QString fileId = m_notebookService->createFile(p_nbId, QString(), p_name);
  QVERIFY(!fileId.isEmpty());
  QString buf = m_bufferCore->openBuffer(p_nbId, p_name);
  QVERIFY(!buf.isEmpty());
}

QListView *TestHistorySticker::listViewOf(HistorySticker *p_sticker) const {
  return p_sticker->findChild<QListView *>();
}

void TestHistorySticker::sendShow(HistorySticker *p_sticker) const {
  QShowEvent event;
  QApplication::sendEvent(p_sticker, &event);
}

void TestHistorySticker::test_factoryRegistersHistory() {
  StickerFactory factory;
  factory.registerBuiltInCreators();
  QVERIFY(factory.registeredTypes().contains(QStringLiteral("history")));

  Sticker *sticker = factory.create(QStringLiteral("history"), *m_services, QJsonObject());
  QVERIFY(sticker != nullptr);
  QCOMPARE(sticker->typeId(), QStringLiteral("history"));
  QCOMPARE(sticker->titleText(), QStringLiteral("History"));
  delete sticker;
}

void TestHistorySticker::test_firstShowLoadsRecent() {
  openFile(m_nbId, QStringLiteral("a.md"));
  openFile(m_nbId, QStringLiteral("b.md"));

  HistorySticker sticker(*m_services);
  QListView *lv = listViewOf(&sticker);
  QVERIFY(lv != nullptr);
  QVERIFY(lv->model() != nullptr);

  // Before show, model has not been loaded.
  QCOMPARE(lv->model()->rowCount(), 0);

  sendShow(&sticker);
  QCOMPARE(lv->model()->rowCount(), 2);
  sticker.hide();
}

void TestHistorySticker::test_calendarHookSwitchesToDate() {
  openFile(m_nbId, QStringLiteral("c.md"));

  HistorySticker sticker(*m_services);
  sendShow(&sticker);
  QListView *lv = listViewOf(&sticker);
  QCOMPARE(lv->model()->rowCount(), 1);

  // Today's local date: the just-opened file matches.
  QVariantMap args;
  args[QStringLiteral("date")] = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
  QCOMPARE(lv->model()->rowCount(), 1);

  // A different day excludes the entry.
  QVariantMap other;
  other[QStringLiteral("date")] =
      QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, other);
  QCOMPARE(lv->model()->rowCount(), 0);
  sticker.hide();
}

void TestHistorySticker::test_invalidPayloadPreservesMode() {
  openFile(m_nbId, QStringLiteral("d.md"));

  HistorySticker sticker(*m_services);
  sendShow(&sticker);
  QListView *lv = listViewOf(&sticker);
  QCOMPARE(lv->model()->rowCount(), 1); // recent mode

  // Missing "date" key: mode preserved (still recent).
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, QVariantMap());
  QCOMPARE(lv->model()->rowCount(), 1);

  // Invalid date string: mode preserved.
  QVariantMap bad;
  bad[QStringLiteral("date")] = QStringLiteral("not-a-date");
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, bad);
  QCOMPARE(lv->model()->rowCount(), 1);
  sticker.hide();
}

void TestHistorySticker::test_secondShowRefreshesActiveMode() {
  openFile(m_nbId, QStringLiteral("e.md"));

  HistorySticker sticker(*m_services);
  sendShow(&sticker);
  QListView *lv = listViewOf(&sticker);

  // Switch to a past day (day-filter mode with no matching entries).
  QVariantMap other;
  other[QStringLiteral("date")] =
      QDate::currentDate().addDays(-2).toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, other);
  QCOMPARE(lv->model()->rowCount(), 0);

  sticker.hide();
  // Second show must refresh the ACTIVE (day-filter) mode, not reset to recent.
  sendShow(&sticker);
  QCOMPARE(lv->model()->rowCount(), 0);
  sticker.hide();
}

void TestHistorySticker::test_destructorUnregistersCallback() {
  const int before = m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged);

  auto *sticker = new HistorySticker(*m_services);
  QCOMPARE(m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged), before + 1);

  delete sticker;
  QCOMPARE(m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged), before);

  // Dispatching after destruction must not invoke a destroyed object / crash.
  QVariantMap args;
  args[QStringLiteral("date")] = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
}

void TestHistorySticker::test_activationOpensExpectedNode() {
  openFile(m_nbId, QStringLiteral("open_me.md"));

  HistorySticker sticker(*m_services);
  sendShow(&sticker);
  QListView *lv = listViewOf(&sticker);
  QCOMPARE(lv->model()->rowCount(), 1);

  // Capture the NodeIdentifier reaching the open path via the FileBeforeOpen hook.
  QString capturedNotebookId;
  QString capturedPath;
  const int hookId = m_hookManager->addAction(
      HookNames::FileBeforeOpen,
      [&](HookContext &, const QVariantMap &p_args) {
        capturedNotebookId = p_args.value(QStringLiteral("notebookId")).toString();
        capturedPath = p_args.value(QStringLiteral("filePath")).toString();
      });

  const QModelIndex idx = lv->model()->index(0, 0);
  QVERIFY(idx.isValid());
  QMetaObject::invokeMethod(lv, "activated", Q_ARG(QModelIndex, idx));

  QCOMPARE(capturedNotebookId, m_nbId);
  QCOMPARE(capturedPath, QStringLiteral("open_me.md"));

  // Remove the action before the captured stack references go out of scope so a
  // later test opening a file can never invoke a dangling callback.
  m_hookManager->removeAction(hookId);
  sticker.hide();
}

void TestHistorySticker::test_clearFilterReturnsToRecent() {
  openFile(m_nbId, QStringLiteral("f.md"));
  openFile(m_nbId, QStringLiteral("g.md"));

  HistorySticker sticker(*m_services);
  sendShow(&sticker);
  QListView *lv = listViewOf(&sticker);
  QCOMPARE(lv->model()->rowCount(), 2);

  // Recent mode: the filter bar is hidden.
  auto *filterBar = sticker.findChild<QWidget *>(QStringLiteral("historyFilterBar"));
  QVERIFY(filterBar != nullptr);
  QVERIFY(filterBar->isHidden());

  // Switch to today's date: filter bar becomes visible.
  QVariantMap args;
  args[QStringLiteral("date")] = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
  QVERIFY(!filterBar->isHidden());

  // Click the clear button: back to recent mode, bar hidden.
  auto *clearButton = sticker.findChild<QToolButton *>(QStringLiteral("historyClearFilter"));
  QVERIFY(clearButton != nullptr);
  clearButton->click();
  QCOMPARE(lv->model()->rowCount(), 2);
  QVERIFY(filterBar->isHidden());

  // A subsequent show keeps recent mode, proving m_activeDate was cleared.
  sendShow(&sticker);
  QCOMPARE(lv->model()->rowCount(), 2);
  QVERIFY(filterBar->isHidden());
  sticker.hide();
}

} // namespace tests

QTEST_MAIN(tests::TestHistorySticker)
#include "test_historysticker.moc"
