#include <QtTest>

#include <QScreen>
#include <QToolButton>

#include <widgets/buttonpopup.h>

using namespace vnotex;

namespace tests {
class TestButtonPopup : public QObject {
  Q_OBJECT

private slots:
  void editorPopupStaysAnchoredAtScreenEdges_data();
  void editorPopupStaysAnchoredAtScreenEdges();
  void nativeAndHiddenButtonsKeepQtPlacement_data();
  void nativeAndHiddenButtonsKeepQtPlacement();
};

void TestButtonPopup::editorPopupStaysAnchoredAtScreenEdges_data() {
  QTest::addColumn<bool>("rightEdge");
  QTest::newRow("left-edge-clamps") << false;
  QTest::newRow("right-edge-does-not-double-shift") << true;
}

void TestButtonPopup::editorPopupStaysAnchoredAtScreenEdges() {
  QFETCH(bool, rightEdge);
  QWidget window(nullptr, Qt::Window | Qt::FramelessWindowHint);
  const auto available = window.screen()->availableGeometry();
  window.setGeometry(available);
  QToolButton button(&window);
  button.setGeometry(rightEdge ? available.width() - 60 : 10, 20, 40, 30);
  ButtonPopup popup(&button, &window, ButtonPopup::Alignment::Right);
  popup.addAction(QStringLiteral("Entry"));
  popup.setFixedSize(qMin(320, available.width() / 2), 120);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  const auto anchor = button.mapToGlobal(QPoint(0, button.height()));
  QPoint firstPosition;
  for (int pass = 0; pass < 3; ++pass) {
    popup.popup(anchor);
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    QVERIFY(available.contains(popup.geometry()));
    if (rightEdge) {
      QCOMPARE(popup.geometry().right(), button.mapToGlobal(QPoint(button.width(), 0)).x() - 1);
    } else {
      QCOMPARE(popup.x(), available.left());
    }
    if (pass == 0) {
      firstPosition = popup.pos();
    } else {
      QCOMPARE(popup.pos(), firstPosition);
    }
    popup.hide();
  }
}

void TestButtonPopup::nativeAndHiddenButtonsKeepQtPlacement_data() {
  QTest::addColumn<bool>("hiddenButton");
  QTest::newRow("non-editor-native-placement") << false;
  QTest::newRow("toolbar-overflow-hidden-button") << true;
}

void TestButtonPopup::nativeAndHiddenButtonsKeepQtPlacement() {
  QFETCH(bool, hiddenButton);
  QWidget window(nullptr, Qt::Window | Qt::FramelessWindowHint);
  const auto available = window.screen()->availableGeometry();
  window.setGeometry(available);
  QToolButton button(&window);
  button.setGeometry(10, 20, 40, 30);
  button.setVisible(!hiddenButton);
  ButtonPopup popup(&button, &window,
                    hiddenButton ? ButtonPopup::Alignment::Right : ButtonPopup::Alignment::Native);
  popup.addAction(QStringLiteral("Entry"));
  popup.setFixedSize(240, 120);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  // A requested point away from the button models Qt placing an overflow submenu.
  const auto requested = available.center();
  popup.popup(requested);
  QVERIFY(QTest::qWaitForWindowExposed(&popup));
  QCOMPARE(popup.pos(), requested);
}
} // namespace tests

QTEST_MAIN(tests::TestButtonPopup)
#include "test_buttonpopup.moc"
