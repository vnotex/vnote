#include <QtTest>

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

void TestTableInsertPopup::testHoverAndClickEmitsBodyRowsAndColumns() {
  QSignalSpy selected(m_popup, &TableInsertPopup::tableSelected);
  const QPoint cell(m_grid->width() * 7 / 14, m_grid->height() * 5 / 14);
  QTest::mouseMove(m_grid, cell);
  QTRY_COMPARE(m_popup->getHoveredBodyRows(), 3);
  QCOMPARE(m_popup->getHoveredColumns(), 4);

  QTest::mouseClick(m_grid, Qt::LeftButton, Qt::NoModifier, cell);
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.at(0).at(0).toInt(), 3);
  QCOMPARE(selected.at(0).at(1).toInt(), 4);
  QVERIFY(!m_popup->isVisible());
}

void TestTableInsertPopup::testOutsideGridClickIgnored() {
  QSignalSpy selected(m_popup, &TableInsertPopup::tableSelected);
  QTest::mouseMove(m_grid, QPoint(m_grid->width() * 13 / 14, m_grid->height() * 13 / 14));
  QTRY_COMPARE(m_popup->getHoveredBodyRows(), 7);
  QCOMPARE(m_popup->getHoveredColumns(), 7);

  const QPoint outside(m_grid->width() + 1, m_grid->height() / 2);
  QTest::mouseMove(m_grid, outside);
  QTRY_COMPARE(m_popup->getHoveredBodyRows(), 0);
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
