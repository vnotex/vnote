#include <QtTest>

#include <QMouseEvent>
#include <QToolButton>

#include <widgets/tableinsertpopup.h>

using namespace vnotex;

namespace tests {
class TestTableInsertPopup : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void testHoverAndClickEmitsBodyRowsAndColumns();
  void testOutsideGridClickIgnored();
  void testDialogFallbackEmitsRequest();

private:
  void movePointer(const QPoint &p_pos);

  QToolButton m_button;
  TableInsertPopup *m_popup = nullptr;
  QWidget *m_grid = nullptr;
};

void TestTableInsertPopup::init() {
  m_popup = new TableInsertPopup(&m_button);
  m_grid = m_popup->findChild<QWidget *>(QStringLiteral("tableInsertGrid"));
  QVERIFY(m_grid);
  // Exercise the embedded widget without QMenu::popup()'s mouse grab.
  m_popup->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_popup));
}

void TestTableInsertPopup::cleanup() {
  delete m_popup;
  m_popup = nullptr;
  m_grid = nullptr;
}

void TestTableInsertPopup::movePointer(const QPoint &p_pos) {
  // Test Qt input handling without relying on native cursor warping on CI desktops.
  QMouseEvent move(QEvent::MouseMove, p_pos, m_grid->mapToGlobal(p_pos), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
  QCoreApplication::sendEvent(m_grid, &move);
}

void TestTableInsertPopup::testHoverAndClickEmitsBodyRowsAndColumns() {
  QSignalSpy selected(m_popup, &TableInsertPopup::tableSelected);
  const QPoint cell(m_grid->width() * 7 / 14, m_grid->height() * 5 / 14);
  movePointer(cell);
  QCOMPARE(m_popup->getHoveredBodyRows(), 3);
  QCOMPARE(m_popup->getHoveredColumns(), 4);

  QTest::mouseClick(m_grid, Qt::LeftButton, Qt::NoModifier, cell);
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.at(0).at(0).toInt(), 3);
  QCOMPARE(selected.at(0).at(1).toInt(), 4);
  QVERIFY(!m_popup->isVisible());
}

void TestTableInsertPopup::testOutsideGridClickIgnored() {
  QSignalSpy selected(m_popup, &TableInsertPopup::tableSelected);
  movePointer(QPoint(m_grid->width() * 13 / 14, m_grid->height() * 13 / 14));
  QCOMPARE(m_popup->getHoveredBodyRows(), 7);
  QCOMPARE(m_popup->getHoveredColumns(), 7);

  const QPoint outside(m_grid->width() + 1, m_grid->height() / 2);
  QEvent leave(QEvent::Leave);
  QCoreApplication::sendEvent(m_grid, &leave);
  QCOMPARE(m_popup->getHoveredBodyRows(), 0);
  QCOMPARE(m_popup->getHoveredColumns(), 0);
  QTest::mouseClick(m_grid, Qt::LeftButton, Qt::NoModifier, outside);
  QCOMPARE(selected.count(), 0);
}

void TestTableInsertPopup::testDialogFallbackEmitsRequest() {
  QSignalSpy requested(m_popup, &TableInsertPopup::dialogRequested);
  QSignalSpy selected(m_popup, &TableInsertPopup::tableSelected);
  auto *button = m_popup->findChild<QToolButton *>(QStringLiteral("tableInsertDialogButton"));
  QVERIFY(button);
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requested.count(), 1);
  QCOMPARE(selected.count(), 0);
  QVERIFY(!m_popup->isVisible());
}
} // namespace tests

QTEST_MAIN(tests::TestTableInsertPopup)
#include "test_tableinsertpopup.moc"
