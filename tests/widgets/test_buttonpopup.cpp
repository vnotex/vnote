#include <QtTest>

#include <QLineEdit>
#include <QScreen>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>

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
  void embeddedInputAndClickAwayDismissal();
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
    const auto geometry = popup.windowHandle()->geometry();
    QVERIFY(available.contains(geometry));
    if (rightEdge) {
      QCOMPARE(geometry.right(), button.mapToGlobal(QPoint(button.width(), 0)).x() - 1);
    } else {
      // QMenu omits platform frame margins from QWidget::frameGeometry().
      QCOMPARE(popup.windowHandle()->frameGeometry().left(), available.left());
    }
    if (pass == 0) {
      firstPosition = geometry.topLeft();
    } else {
      QCOMPARE(geometry.topLeft(), firstPosition);
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
  // Native placement need not equal the requested point (notably on macOS).
  QMenu nativeMenu(&window);
  nativeMenu.setWindowFlags(popup.windowFlags());
  nativeMenu.setWindowModality(popup.windowModality());
  nativeMenu.addAction(QStringLiteral("Entry"));
  nativeMenu.setFixedSize(popup.size());
  nativeMenu.popup(requested);
  QVERIFY(QTest::qWaitForWindowExposed(&nativeMenu));
  const auto nativePosition = nativeMenu.pos();
  nativeMenu.hide();

  popup.popup(requested);
  QVERIFY(QTest::qWaitForWindowExposed(&popup));
  QCOMPARE(popup.pos(), nativePosition);
}

void TestButtonPopup::embeddedInputAndClickAwayDismissal() {
  QWidget window;
  window.resize(640, 480);
  QToolButton button(&window);
  button.setGeometry(20, 20, 100, 30);
  ButtonPopup popup(&button, &window);
  auto *edit = new QLineEdit;
  auto *action = new QWidgetAction(&popup);
  action->setDefaultWidget(edit);
  popup.addAction(action);
  QSignalSpy submitted(edit, &QLineEdit::returnPressed);
  QSignalSpy hidden(&popup, &QMenu::aboutToHide);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&window));

  const auto anchor = button.mapToGlobal(QPoint(0, button.height()));
  popup.popup(anchor);
  QVERIFY(QTest::qWaitForWindowExposed(&popup));
  // A modal Tool window restores the macOS title bar and loses popup dismissal.
  QCOMPARE(popup.windowType(), Qt::Popup);
  QCOMPARE(popup.windowModality(), Qt::NonModal);
  QCOMPARE(QApplication::activePopupWidget(), &popup);
  QTest::mouseClick(edit, Qt::LeftButton);
  QTRY_VERIFY(edit->hasFocus());
  QTest::keyClicks(QApplication::focusWidget(), "new-tag");
  QCOMPARE(edit->text(), QStringLiteral("new-tag"));
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
  QCOMPARE(submitted.count(), 1);
  QVERIFY(popup.isVisible());
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Enter);
  QCOMPARE(submitted.count(), 2);
  QVERIFY(popup.isVisible());

  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
  QTRY_VERIFY(!popup.isVisible());
  QCOMPARE(hidden.count(), 1);

  popup.popup(anchor);
  QVERIFY(QTest::qWaitForWindowExposed(&popup));
  QCOMPARE(edit->text(), QStringLiteral("new-tag"));
  const auto outside = window.mapToGlobal(QPoint(window.width() - 20, window.height() - 20));
  QVERIFY(!popup.frameGeometry().contains(outside));
  // Use window-system routing so the popup grab handles a press outside its bounds.
  QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                    window.mapFromGlobal(outside));
  QTRY_VERIFY(!popup.isVisible());
  QCOMPARE(hidden.count(), 2);
}
} // namespace tests

QTEST_MAIN(tests::TestButtonPopup)
#include "test_buttonpopup.moc"
