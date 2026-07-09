#include <QtTest>

#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <core/servicelocator.h>
#include <unitedentry/iviewwindownavigator.h>
#include <unitedentry/unitedentrymgr.h>
#include <unitedentry/windowsunitedentry.h>

using namespace vnotex;

namespace tests {

// Fake navigator returning canned OpenWindowEntry rows and recording focus calls.
class FakeNavigator : public IViewWindowNavigator {
public:
  QVector<OpenWindowEntry> m_entries;
  QString m_lastFocusWorkspaceId;
  QString m_lastFocusBufferId;
  int m_focusCallCount = 0;

  QVector<OpenWindowEntry> listOpenWindows() const override { return m_entries; }

  void focusWindow(const QString &p_workspaceId, const QString &p_bufferId) override {
    ++m_focusCallCount;
    m_lastFocusWorkspaceId = p_workspaceId;
    m_lastFocusBufferId = p_bufferId;
  }
};

class TestWindowsUnitedEntry : public QObject {
  Q_OBJECT

private slots:
  void testVisibleAndHiddenAppear();
  void testZeroWindowWorkspaceSkipped();
  void testFilterByArgs();
  void testCurrentPreselectedAndExpanded();
  void testActivateChildFocusesWindow();
  void testNoNavigator();

private:
  static OpenWindowEntry makeEntry(const QString &p_wsId, const QString &p_wsName, bool p_visible,
                                   ID p_winId, const QString &p_bufferId,
                                   const QString &p_winName, bool p_winCurrent = false);

  static QSharedPointer<QWidget> runProcess(WindowsUnitedEntry &p_entry, const QString &p_args);
};

OpenWindowEntry TestWindowsUnitedEntry::makeEntry(const QString &p_wsId, const QString &p_wsName,
                                                  bool p_visible, ID p_winId,
                                                  const QString &p_bufferId,
                                                  const QString &p_winName, bool p_winCurrent) {
  OpenWindowEntry e;
  e.workspaceId = p_wsId;
  e.workspaceName = p_wsName;
  e.workspaceVisible = p_visible;
  e.windowId = p_winId;
  e.bufferId = p_bufferId;
  e.windowName = p_winName;
  e.windowTitle = p_winName;
  e.windowCurrent = p_winCurrent;
  return e;
}

QSharedPointer<QWidget> TestWindowsUnitedEntry::runProcess(WindowsUnitedEntry &p_entry,
                                                           const QString &p_args) {
  QSharedPointer<QWidget> captured;
  p_entry.process(p_args, [&captured](const QSharedPointer<QWidget> &p_widget) {
    captured = p_widget;
  });
  return captured;
}

void TestWindowsUnitedEntry::testVisibleAndHiddenAppear() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  FakeNavigator nav;
  nav.m_entries = {
      makeEntry("ws1", "Alpha", true, 1, "buf1", "file1.md"),
      makeEntry("ws1", "Alpha", true, 2, "buf2", "file2.md"),
      makeEntry("ws2", "Beta", false, 3, "buf3", "file3.md"),
  };
  mgr.setViewWindowNavigator(&nav);

  WindowsUnitedEntry entry(services, &mgr);
  auto widget = runProcess(entry, QString());
  auto *tree = qobject_cast<QTreeWidget *>(widget.data());
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 2);

  auto *alpha = tree->topLevelItem(0);
  QCOMPARE(alpha->text(0), QStringLiteral("Alpha"));
  QCOMPARE(alpha->childCount(), 2);

  auto *beta = tree->topLevelItem(1);
  // Hidden workspaces get a " (hidden)" suffix.
  QVERIFY(beta->text(0).startsWith(QStringLiteral("Beta")));
  QVERIFY(beta->text(0).contains(QStringLiteral("hidden")));
  QCOMPARE(beta->childCount(), 1);
}

void TestWindowsUnitedEntry::testZeroWindowWorkspaceSkipped() {
  // A workspace with no rows simply never appears (navigator already skips
  // empty workspaces); verify nothing spurious is shown.
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  FakeNavigator nav;
  nav.m_entries = {
      makeEntry("ws1", "Alpha", true, 1, "buf1", "file1.md"),
  };
  mgr.setViewWindowNavigator(&nav);

  WindowsUnitedEntry entry(services, &mgr);
  auto widget = runProcess(entry, QString());
  auto *tree = qobject_cast<QTreeWidget *>(widget.data());
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 1);
}

void TestWindowsUnitedEntry::testFilterByArgs() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  FakeNavigator nav;
  nav.m_entries = {
      makeEntry("ws1", "Alpha", true, 1, "buf1", "readme.md"),
      makeEntry("ws1", "Alpha", true, 2, "buf2", "notes.txt"),
      makeEntry("ws2", "Beta", false, 3, "buf3", "other.md"),
  };
  mgr.setViewWindowNavigator(&nav);

  WindowsUnitedEntry entry(services, &mgr);
  auto widget = runProcess(entry, QStringLiteral("notes"));
  auto *tree = qobject_cast<QTreeWidget *>(widget.data());
  QVERIFY(tree);
  // Only ws1/notes.txt matches; Beta parent has no match and is dropped.
  QCOMPARE(tree->topLevelItemCount(), 1);
  QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Alpha"));
  QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
  QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("notes.txt"));
}

void TestWindowsUnitedEntry::testCurrentPreselectedAndExpanded() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  FakeNavigator nav;
  nav.m_entries = {
      makeEntry("ws1", "Alpha", true, 1, "buf1", "file1.md"),
      makeEntry("ws2", "Beta", true, 2, "buf2", "file2.md", /*winCurrent=*/true),
  };
  mgr.setViewWindowNavigator(&nav);

  WindowsUnitedEntry entry(services, &mgr);
  auto widget = runProcess(entry, QString());
  auto *tree = qobject_cast<QTreeWidget *>(widget.data());
  QVERIFY(tree);

  auto *current = tree->currentItem();
  QVERIFY(current);
  QCOMPARE(current->text(0), QStringLiteral("file2.md"));
  QVERIFY(current->parent());
  QVERIFY(current->parent()->isExpanded());
}

void TestWindowsUnitedEntry::testActivateChildFocusesWindow() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  FakeNavigator nav;
  nav.m_entries = {
      makeEntry("ws2", "Beta", false, 3, "buf3", "file3.md"),
  };
  mgr.setViewWindowNavigator(&nav);

  WindowsUnitedEntry entry(services, &mgr);

  int activatedCount = 0;
  bool quit = false;
  bool restoreFocus = true;
  connect(&entry, &IUnitedEntry::itemActivated, &entry,
          [&](bool p_quit, bool p_restoreFocus) {
            ++activatedCount;
            quit = p_quit;
            restoreFocus = p_restoreFocus;
          });

  auto widget = runProcess(entry, QString());
  auto *tree = qobject_cast<QTreeWidget *>(widget.data());
  QVERIFY(tree);
  auto *child = tree->topLevelItem(0)->child(0);
  QVERIFY(child);

  // Emit the tree's itemActivated signal to drive the private slot.
  emit tree->itemActivated(child, 0);

  QCOMPARE(nav.m_focusCallCount, 1);
  QCOMPARE(nav.m_lastFocusWorkspaceId, QStringLiteral("ws2"));
  QCOMPARE(nav.m_lastFocusBufferId, QStringLiteral("buf3"));
  QCOMPARE(activatedCount, 1);
  QVERIFY(quit);
  QVERIFY(!restoreFocus);
}

void TestWindowsUnitedEntry::testNoNavigator() {
  ServiceLocator services;
  UnitedEntryMgr mgr(services);
  // No navigator set.

  WindowsUnitedEntry entry(services, &mgr);
  auto widget = runProcess(entry, QString());
  auto *label = qobject_cast<QLabel *>(widget.data());
  QVERIFY(label);
}

} // namespace tests

QTEST_MAIN(tests::TestWindowsUnitedEntry)
#include "test_windowsunitedentry.moc"
