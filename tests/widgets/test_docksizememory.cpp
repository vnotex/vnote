#include <QtTest>

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTextEdit>
#include <QVBoxLayout>

#include <widgets/docksizememory.h>

using namespace vnotex;

namespace {
struct Layout {
  QMainWindow window;
  QDockWidget *outline = new QDockWidget(QStringLiteral("Outline"), &window);
  QDockWidget *dock = new QDockWidget(QStringLiteral("Tools"), &window);
  QWidget *host = new QWidget;
  DockSizeMemory *memory = new DockSizeMemory(&window, dock);

  Layout() {
    window.resize(1000, 800);
    window.setCentralWidget(new QTextEdit);
    outline->setObjectName(QStringLiteral("Outline"));
    dock->setObjectName(QStringLiteral("Tools"));
    outline->setWidget(new QLabel(QStringLiteral("Outline")));
    new QVBoxLayout(host);
    dock->setWidget(host);
    dock->setMinimumWidth(150);
    window.addDockWidget(Qt::RightDockWidgetArea, outline);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    window.splitDockWidget(outline, dock, Qt::Vertical);
    dock->hide();
  }

  void attachContent() { host->layout()->addWidget(new QTextEdit); }

  void show() {
    window.show();
    QTest::qWait(30);
  }

  void resizeDock(int p_width, int p_height) {
    QTest::qWait(30);
    window.resizeDocks({dock}, {p_width}, Qt::Horizontal);
    window.resizeDocks({outline, dock}, {outline->height() + dock->height() - p_height, p_height},
                       Qt::Vertical);
    QTest::qWait(30);
  }
};
} // namespace

class TestDockSizeMemory : public QObject {
  Q_OBJECT
private slots:
  void restoresHiddenDockAfterContentIsAttached();
  void remembersExplicitHide();
  void rapidShowHideKeepsSavedSize();
  void parentHideShowKeepsLiveResize();
  void resetCancelsPendingRestore();
  void floatingGeometryDoesNotReplaceDockedSize();
  void tabbedGeometryDoesNotReplaceDockedSize();
};

void TestDockSizeMemory::restoresHiddenDockAfterContentIsAttached() {
  QByteArray state;
  QSize size;
  {
    Layout old;
    old.attachContent();
    old.dock->show();
    old.show();
    old.resizeDock(260, 370);
    size = old.dock->size();
    old.dock->hide();
    QTest::qWait(30);
    state = old.window.saveState();
    QCOMPARE(old.memory->getSize(), size);
  }

  Layout restored;
  QVERIFY(restored.window.restoreState(state));
  restored.memory->setSize(size);
  restored.show();
  // The real Tools host is empty until a supporting editor becomes current.
  restored.attachContent();
  restored.dock->show();
  QTRY_COMPARE(restored.dock->size(), size);
}

void TestDockSizeMemory::remembersExplicitHide() {
  Layout layout;
  layout.attachContent();
  layout.dock->show();
  layout.show();
  layout.resizeDock(260, 370);
  const auto size = layout.dock->size();
  layout.dock->hide();
  QCOMPARE(layout.memory->getSize(), size);
  // A different editor can change the remaining dock column while Tools is hidden.
  layout.window.resizeDocks({layout.outline}, {400}, Qt::Horizontal);
  QTest::qWait(30);
  layout.dock->show();
  QTRY_COMPARE(layout.dock->size(), size);
}

void TestDockSizeMemory::rapidShowHideKeepsSavedSize() {
  Layout layout;
  const QSize size(260, 370);
  layout.memory->setSize(size);
  layout.show();
  layout.attachContent();
  layout.dock->show();
  layout.dock->hide();
  QTest::qWait(30);
  QCOMPARE(layout.memory->getSize(), size);
  layout.dock->show();
  QTRY_COMPARE(layout.dock->size(), size);
}

void TestDockSizeMemory::parentHideShowKeepsLiveResize() {
  Layout layout;
  layout.memory->setSize(QSize(260, 370));
  layout.attachContent();
  layout.dock->show();
  layout.show();
  QTRY_COMPARE(layout.dock->size(), QSize(260, 370));
  layout.resizeDock(310, 440);
  const auto size = layout.dock->size();
  layout.window.hide();
  QVERIFY(!layout.dock->isHidden());
  layout.show();
  QCOMPARE(layout.dock->size(), size);
  QCOMPARE(layout.memory->getSize(), size);
}

void TestDockSizeMemory::resetCancelsPendingRestore() {
  Layout layout;
  layout.memory->setSize(QSize(260, 370));
  layout.show();
  layout.attachContent();
  layout.dock->show();
  layout.memory->setSize(QSize());
  layout.resizeDock(310, 440);
  QCOMPARE(layout.dock->size(), QSize(310, 440));
  QCOMPARE(layout.memory->getSize(), QSize(310, 440));
}

void TestDockSizeMemory::floatingGeometryDoesNotReplaceDockedSize() {
  Layout layout;
  const QSize size(260, 370);
  layout.memory->setSize(size);
  layout.attachContent();
  layout.dock->setFloating(true);
  layout.dock->resize(430, 510);
  layout.dock->show();
  layout.show();
  QCOMPARE(layout.dock->size(), QSize(430, 510));
  QCOMPARE(layout.memory->getSize(), size);
  layout.dock->hide();
  QCOMPARE(layout.memory->getSize(), size);
  layout.dock->setFloating(false);
  layout.dock->show();
  QTRY_COMPARE(layout.dock->size(), size);
}

void TestDockSizeMemory::tabbedGeometryDoesNotReplaceDockedSize() {
  Layout layout;
  const QSize size(260, 370);
  layout.memory->setSize(size);
  layout.attachContent();
  layout.window.tabifyDockWidget(layout.outline, layout.dock);
  layout.dock->show();
  layout.show();
  layout.dock->raise();
  QTest::qWait(30);
  const auto tabbedSize = layout.dock->size();
  QVERIFY(tabbedSize.height() > size.height());
  QCOMPARE(layout.memory->getSize(), size);
  layout.dock->hide();
  layout.dock->show();
  QTest::qWait(30);
  QCOMPARE(layout.dock->size(), tabbedSize);
  layout.dock->hide();
  layout.window.removeDockWidget(layout.dock);
  layout.window.addDockWidget(Qt::RightDockWidgetArea, layout.dock);
  layout.window.splitDockWidget(layout.outline, layout.dock, Qt::Vertical);
  layout.dock->show();
  QTRY_COMPARE(layout.dock->size(), size);
}

QTEST_MAIN(TestDockSizeMemory)
#include "test_docksizememory.moc"
