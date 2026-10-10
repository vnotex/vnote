// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include <core/servicelocator.h>
#include <widgets/toolspanelhost.h>

using namespace vnotex;

namespace tests {
namespace {

struct Panel {
  QPointer<QWidget> widget;
  QLineEdit *edit = nullptr;

  explicit Panel(QWidget *p_owner) : widget(new QWidget(p_owner)) {
    auto *layout = new QVBoxLayout(widget);
    edit = new QLineEdit(widget);
    layout->addWidget(edit);
    widget->setFocusProxy(edit);
  }
};

struct Fixture {
  ServiceLocator services;
  QMainWindow window;
  QLineEdit *editor = new QLineEdit(&window);
  QWidget *ownerA = new QWidget(&window);
  QWidget *ownerB = new QWidget(&window);
  QDockWidget *dock = new QDockWidget(&window);
  ToolsPanelHost *host = new ToolsPanelHost(services, dock);
  Panel panelA{ownerA};
  Panel panelB{ownerB};

  Fixture() {
    window.setCentralWidget(editor);
    dock->setWidget(host);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    ownerA->hide();
    ownerB->hide();
    window.resize(640, 400);
  }

  bool show() {
    window.show();
    window.raise();
    window.activateWindow();
    return QTest::qWaitForWindowExposed(&window);
  }
};

void drainDeletes() {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
}

} // namespace

class TestToolsPanelHost : public QObject {
  Q_OBJECT
private slots:
  void switchingPreservesEditsAndFocus();
  void inactiveOwnerDeletesOnlyItsPanel();
  void activeOwnerDeletionEmptiesReusableHost();
  void panelFirstDeletionAllowsReplacement();
  void dockFirstDeletionReturnsReusablePanel();
  void deferredDeletion_data();
  void deferredDeletion();
  void sharedParentDeletion_data();
  void sharedParentDeletion();
};

void TestToolsPanelHost::switchingPreservesEditsAndFocus() {
  Fixture f;
  QVERIFY(f.show());
  f.editor->setFocus();
  QTRY_VERIFY(f.editor->hasFocus());
  f.host->setPanel(f.ownerA, f.panelA.widget);
  QTRY_VERIFY(f.editor->hasFocus());
  QVERIFY(f.panelA.widget->isVisible());
  f.panelA.edit->setFocus();
  QTRY_VERIFY(f.panelA.edit->hasFocus());
  QTest::keyClicks(f.panelA.edit, "owner-A");
  f.host->setPanel(f.ownerA, f.panelA.widget);
  QVERIFY(f.panelA.edit->hasFocus());
  QCOMPARE(f.panelA.edit->text(), QStringLiteral("owner-A"));

  f.editor->setFocus();
  f.host->setPanel(f.ownerB, f.panelB.widget);
  QTRY_VERIFY(f.editor->hasFocus());
  QVERIFY(f.panelA.widget && f.panelA.widget->isHidden());
  QTest::keyClicks(f.panelB.edit, "owner-B");
  f.host->setPanel(f.ownerA, f.panelA.widget);
  QCOMPARE(f.host->getPanel(), f.panelA.widget.data());
  QCOMPARE(f.panelA.edit->text(), QStringLiteral("owner-A"));
  QVERIFY(f.panelB.widget && f.panelB.widget->isHidden());
  QCOMPARE(f.panelB.edit->text(), QStringLiteral("owner-B"));

  f.panelA.edit->setFocus();
  QTRY_VERIFY(f.panelA.edit->hasFocus());
  f.host->setPanel(nullptr, nullptr);
  QVERIFY(f.panelA.widget->isHidden());
  QVERIFY(QApplication::focusWidget() != f.panelA.edit);
  QVERIFY(!f.host->getPanel());
}

void TestToolsPanelHost::inactiveOwnerDeletesOnlyItsPanel() {
  Fixture f;
  QSignalSpy destroyedA(f.panelA.widget, &QObject::destroyed);
  QSignalSpy destroyedB(f.panelB.widget, &QObject::destroyed);
  f.host->setPanel(f.ownerA, f.panelA.widget);
  f.host->setPanel(nullptr, nullptr);
  f.host->setPanel(f.ownerB, f.panelB.widget);
  delete f.ownerA;
  QCOMPARE(destroyedA.count(), 1);
  QCOMPARE(destroyedB.count(), 0);
  QCOMPARE(f.host->getPanel(), f.panelB.widget.data());
}

void TestToolsPanelHost::activeOwnerDeletionEmptiesReusableHost() {
  Fixture f;
  QSignalSpy destroyed(f.panelA.widget, &QObject::destroyed);
  f.host->setPanel(f.ownerA, f.panelA.widget);
  delete f.ownerA;
  QVERIFY(!f.host->getPanel());
  QVERIFY(f.panelA.widget.isNull());
  QCOMPARE(destroyed.count(), 1);
  f.host->setPanel(f.ownerB, f.panelB.widget);
  QCOMPARE(f.host->getPanel(), f.panelB.widget.data());
  QTest::keyClicks(f.panelB.edit, "replacement");
  QCOMPARE(f.panelB.edit->text(), QStringLiteral("replacement"));
}

void TestToolsPanelHost::panelFirstDeletionAllowsReplacement() {
  Fixture f;
  QSignalSpy destroyed(f.panelA.widget, &QObject::destroyed);
  f.host->setPanel(f.ownerA, f.panelA.widget);
  delete f.panelA.widget.data();
  QVERIFY(!f.host->getPanel());
  QCOMPARE(destroyed.count(), 1);
  Panel replacement(f.ownerA);
  f.host->setPanel(f.ownerA, replacement.widget);
  QTest::keyClicks(replacement.edit, "new panel");
  QCOMPARE(replacement.edit->text(), QStringLiteral("new panel"));
  delete f.ownerA;
  QVERIFY(replacement.widget.isNull());
  QVERIFY(!f.host->getPanel());
}

void TestToolsPanelHost::dockFirstDeletionReturnsReusablePanel() {
  Fixture f;
  QVERIFY(f.show());
  QSignalSpy destroyed(f.panelA.widget, &QObject::destroyed);
  f.host->setPanel(f.ownerA, f.panelA.widget);
  QTest::keyClicks(f.panelA.edit, "retained");
  delete f.dock;
  QVERIFY(f.panelA.widget && f.panelA.widget->isHidden());
  QCOMPARE(destroyed.count(), 0);
  auto *dock = new QDockWidget(&f.window);
  auto *host = new ToolsPanelHost(f.services, dock);
  dock->setWidget(host);
  f.window.addDockWidget(Qt::RightDockWidgetArea, dock);
  host->setPanel(f.ownerA, f.panelA.widget);
  dock->show();
  QTRY_VERIFY(f.panelA.widget->isVisible());
  QCOMPARE(f.panelA.edit->text(), QStringLiteral("retained"));
  delete f.ownerA;
  QCOMPARE(destroyed.count(), 1);
  QVERIFY(!host->getPanel());
}

void TestToolsPanelHost::deferredDeletion_data() {
  QTest::addColumn<bool>("ownerFirst");
  QTest::newRow("owner-deleteLater") << true;
  QTest::newRow("dock-deleteLater") << false;
}

void TestToolsPanelHost::deferredDeletion() {
  QFETCH(bool, ownerFirst);
  Fixture f;
  QPointer<ToolsPanelHost> host = f.host;
  QSignalSpy destroyed(f.panelA.widget, &QObject::destroyed);
  f.host->setPanel(f.ownerA, f.panelA.widget);
  if (ownerFirst) {
    f.ownerA->deleteLater();
    drainDeletes();
    QVERIFY(host && !host->getPanel());
    QCOMPARE(destroyed.count(), 1);
    f.dock->deleteLater();
  } else {
    f.dock->deleteLater();
    drainDeletes();
    QVERIFY(!host);
    QVERIFY(f.panelA.widget && f.panelA.widget->isHidden());
    QCOMPARE(destroyed.count(), 0);
    f.ownerA->deleteLater();
  }
  drainDeletes();
  QVERIFY(!host);
  QVERIFY(!f.panelA.widget);
  QCOMPARE(destroyed.count(), 1);
}

void TestToolsPanelHost::sharedParentDeletion_data() {
  QTest::addColumn<bool>("ownerFirst");
  QTest::newRow("owner-child-first") << true;
  QTest::newRow("dock-child-first") << false;
}

void TestToolsPanelHost::sharedParentDeletion() {
  QFETCH(bool, ownerFirst);
  ServiceLocator services;
  auto *window = new QMainWindow;
  QWidget *owner = nullptr;
  if (ownerFirst) {
    owner = new QWidget(window);
  }
  auto *dock = new QDockWidget(window);
  if (!ownerFirst) {
    owner = new QWidget(window);
  }
  QPointer<ToolsPanelHost> host = new ToolsPanelHost(services, dock);
  dock->setWidget(host);
  window->addDockWidget(Qt::RightDockWidgetArea, dock);
  Panel panel(owner);
  QSignalSpy destroyed(panel.widget, &QObject::destroyed);
  host->setPanel(owner, panel.widget);
  delete window;
  QVERIFY(!host);
  QVERIFY(!panel.widget);
  QCOMPARE(destroyed.count(), 1);
}

} // namespace tests

QTEST_MAIN(tests::TestToolsPanelHost)
#include "test_toolspanelhost.moc"
