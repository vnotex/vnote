// T26: Lock-icon badge for read-only notebooks in NotebookSelector2.
//
// Verifies that NotebookSelector2::addNotebookItem decorates a read-only
// notebook's combobox entry with the shared lock icon
// (`:/vnotex/data/core/icons/read_only.svg`) while leaving writable
// notebooks with their name-derived (auto-generated) icon.
//
// Identity check: the lock icon and the RO notebook's item icon both
// originate from the same Qt resource path, so their rendered 16x16
// pixmaps are bytewise-equal. For writable notebooks the auto-generated
// text-rect icon is bytewise-different from the lock pixmap.
//
// Per tests/AGENTS.md: vxcore_set_test_mode(1) BEFORE
// vxcore_context_create. We build a minimal ServiceLocator with only the
// service NotebookSelector2's read-only path touches
// (NotebookCoreService). ConfigMgr2 and ThemeService are intentionally NOT
// registered — the selector's `m_services.get<...>()` returns nullptr in
// that case and the relevant code paths (restoreCurrentNotebook,
// saveCurrentNotebook, navigation labels) handle nullptr defensively.
//
// Subtests:
//   1. testReadOnlyNotebookGetsLockIcon — RO notebook item icon's 16x16
//      pixmap matches the lock asset's 16x16 pixmap bytewise.
//   2. testWritableNotebookHasNoLockIcon — writable notebook item icon's
//      16x16 pixmap does NOT match the lock asset's pixmap.

#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <widgets/notebookselector2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookSelector2ReadOnlyBadge : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testReadOnlyNotebookGetsLockIcon();
  void testWritableNotebookHasNoLockIcon();

private:
  VxCoreContextHandle m_context = nullptr;
  ServiceLocator m_services;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
};

void TestNotebookSelector2ReadOnlyBadge::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context);

  m_notebookService = new NotebookCoreService(m_context);
  m_services.registerService<NotebookCoreService>(m_notebookService);
}

void TestNotebookSelector2ReadOnlyBadge::cleanupTestCase() {
  delete m_notebookService;
  m_notebookService = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookSelector2ReadOnlyBadge::testReadOnlyNotebookGetsLockIcon() {
  // Create a bundled notebook then mark it read-only via the vxcore C ABI.
  const QString nbPath = m_tempDir.filePath(QStringLiteral("nb_ro_badge"));
  const QString configJson =
      QStringLiteral(R"({"name": "RO Notebook", "description": "T26 read-only badge test"})");
  const QString nbId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  const VxCoreError err = vxcore_notebook_set_read_only(m_context, nbId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_notebookService->isNotebookReadOnly(nbId));

  // Selector loads notebooks from the service. After loadNotebooks() the
  // RO notebook's item icon must be the shared lock icon.
  NotebookSelector2 selector(m_services);
  selector.loadNotebooks();
  QVERIFY2(selector.count() >= 1, "Selector should contain at least the RO notebook");

  // Find the RO notebook by GUID.
  int roIdx = -1;
  for (int i = 0; i < selector.count(); ++i) {
    if (selector.itemData(i, NotebookGuidRole).toString() == nbId) {
      roIdx = i;
      break;
    }
  }
  QVERIFY2(roIdx >= 0, "Read-only notebook not found in selector");

  const QIcon itemIcon = selector.itemIcon(roIdx);
  const QIcon lockIcon(QStringLiteral(":/vnotex/data/core/icons/read_only.svg"));

  QVERIFY2(!itemIcon.isNull(), "RO notebook item should have an icon set");
  QVERIFY2(!lockIcon.isNull(),
           "Lock icon asset must be reachable via the Qt resource system (qrc)");

  // Render both at 16x16 and compare bytewise. Both icons come from the
  // same SVG so the pixels must match exactly.
  const QPixmap itemPx = itemIcon.pixmap(16, 16);
  const QPixmap lockPx = lockIcon.pixmap(16, 16);
  QVERIFY(!itemPx.isNull());
  QVERIFY(!lockPx.isNull());
  QCOMPARE(itemPx.toImage(), lockPx.toImage());

  // Tooltip carries the read-only reason for screen-reader / hover affordance.
  const QString tooltip = selector.itemData(roIdx, Qt::ToolTipRole).toString();
  QVERIFY2(tooltip.contains(QStringLiteral("Read-only")),
           qPrintable(QStringLiteral("Tooltip should mention read-only state, got: ") + tooltip));

  // Cleanup: turn off RO and close the notebook so subsequent subtests
  // start fresh.
  vxcore_notebook_set_read_only(m_context, nbId.toUtf8().constData(), false);
  m_notebookService->closeNotebook(nbId);
}

void TestNotebookSelector2ReadOnlyBadge::testWritableNotebookHasNoLockIcon() {
  // Create a writable bundled notebook.
  const QString nbPath = m_tempDir.filePath(QStringLiteral("nb_writable_no_badge"));
  const QString configJson =
      QStringLiteral(R"({"name": "Writable", "description": "T26 writable, no badge"})");
  const QString nbId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());
  QVERIFY(!m_notebookService->isNotebookReadOnly(nbId));

  NotebookSelector2 selector(m_services);
  selector.loadNotebooks();
  QVERIFY2(selector.count() >= 1, "Selector should contain at least the writable notebook");

  int wIdx = -1;
  for (int i = 0; i < selector.count(); ++i) {
    if (selector.itemData(i, NotebookGuidRole).toString() == nbId) {
      wIdx = i;
      break;
    }
  }
  QVERIFY2(wIdx >= 0, "Writable notebook not found in selector");

  const QIcon itemIcon = selector.itemIcon(wIdx);
  const QIcon lockIcon(QStringLiteral(":/vnotex/data/core/icons/read_only.svg"));

  // The writable notebook gets the auto-generated text-rect icon. It is
  // non-null AND its pixels differ from the lock icon.
  QVERIFY2(!itemIcon.isNull(), "Writable notebook should still get an auto-generated icon");

  const QPixmap itemPx = itemIcon.pixmap(16, 16);
  const QPixmap lockPx = lockIcon.pixmap(16, 16);
  QVERIFY(!itemPx.isNull());
  QVERIFY(!lockPx.isNull());
  QVERIFY2(itemPx.toImage() != lockPx.toImage(),
           "Writable notebook icon must NOT match the read-only lock icon");

  // Tooltip should NOT contain the read-only suffix for writable notebooks.
  const QString tooltip = selector.itemData(wIdx, Qt::ToolTipRole).toString();
  QVERIFY2(
      !tooltip.contains(QStringLiteral("Read-only")),
      qPrintable(QStringLiteral("Writable tooltip must not mention read-only, got: ") + tooltip));

  m_notebookService->closeNotebook(nbId);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookSelector2ReadOnlyBadge)
#include "test_notebook_selector2_readonly_badge.moc"
