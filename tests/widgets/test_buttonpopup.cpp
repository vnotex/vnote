#include <QtTest>

#include <QDialog>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>

#include <memory>

#include <widgets/buttonpopup.h>

using namespace vnotex;

namespace tests {
namespace {

bool nativeFocusPlatform() {
  const auto platform = QGuiApplication::platformName();
  return platform == QLatin1String("windows") || platform == QLatin1String("cocoa");
}

class InputPopup : public ButtonPopup {
public:
  using ButtonPopup::addWidget;
  using ButtonPopup::ButtonPopup;
};

struct PopupFixture {
  QWidget m_window;
  QToolBar m_toolbar{&m_window};
  QPlainTextEdit m_editor{&m_window};
  QAction *m_tagAction = nullptr;
  QToolButton *m_button = nullptr;
  InputPopup *m_popup = nullptr;
  QLineEdit *m_input = nullptr;
  const QString m_original = QStringLiteral("Source document must remain unchanged");

  PopupFixture() {
    m_toolbar.setMovable(false);
    for (int index = 0; index < 8; ++index) {
      m_toolbar.addAction(QStringLiteral("Before tags %1").arg(index));
    }
    m_tagAction = m_toolbar.addAction(QStringLiteral("Tags"));
    m_button = qobject_cast<QToolButton *>(m_toolbar.widgetForAction(m_tagAction));
    m_popup = new InputPopup(m_button, &m_toolbar, ButtonPopup::Alignment::Right);
    auto *content = new QWidget;
    auto *inputLayout = new QVBoxLayout(content);
    m_input = new QLineEdit(content);
    inputLayout->addWidget(m_input);
    content->setFocusProxy(m_input);
    m_popup->addWidget(content);
    QObject::connect(m_popup, &QMenu::aboutToShow, content, [content]() { content->setFocus(); });
    m_tagAction->setMenu(m_popup);
    m_button->setPopupMode(QToolButton::InstantPopup);

    auto *layout = new QVBoxLayout(&m_window);
    layout->addWidget(&m_toolbar);
    layout->addWidget(&m_editor, 1);
    m_editor.setPlainText(m_original);
    m_editor.document()->setModified(false);
    m_window.resize(1200, 480);
  }

  bool show() {
    m_window.show();
    m_window.raise();
    m_window.activateWindow();
    m_editor.setFocus();
    return QTest::qWaitForWindowExposed(&m_window) && QTest::qWaitForWindowActive(&m_window);
  }

  QPoint anchor() const { return m_toolbar.mapToGlobal(QPoint(0, m_toolbar.height())); }
};

// A child that claims Escape must receive it before popup dismissal. Synthetic
// preedit does not open a native IME candidate panel, so model only its key claim.
class EscapeClaim : public QObject {
public:
  bool m_received = false;

protected:
  bool eventFilter(QObject *, QEvent *p_event) override {
    if (p_event->type() == QEvent::ShortcutOverride || p_event->type() == QEvent::KeyPress) {
      auto *key = static_cast<QKeyEvent *>(p_event);
      if (key->key() == Qt::Key_Escape) {
        m_received |= p_event->type() == QEvent::KeyPress;
        key->accept();
        return true;
      }
    }
    return false;
  }
};

} // namespace

class TestButtonPopup : public QObject {
  Q_OBJECT

private slots:
  void editorPopupStaysAnchoredAtScreenEdges_data();
  void editorPopupStaysAnchoredAtScreenEdges();
  void nativeAndHiddenButtonsKeepQtPlacement_data();
  void nativeAndHiddenButtonsKeepQtPlacement();
  void embeddedInputAndClickAwayDismissal();
  void nativeInputTargetsThePopup_data();
  void nativeInputTargetsThePopup();
  void ownedChildrenTakeEscapeFirst();
  void pendingActivationSurvivesCloseAndOwnerDestruction();
  void aCancelledAncestorCannotStrandTheInput();
  void aNativeContentWindowDismissesThePopup();
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
  // Editing a tag must not make the document modal.
  QCOMPARE(popup.windowModality(), Qt::NonModal);
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
  // Use window-system routing so native popup dismissal sees the outside press.
  QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                    window.mapFromGlobal(outside));
  QTRY_VERIFY(!popup.isVisible());
  QCOMPARE(hidden.count(), 2);
}

void TestButtonPopup::nativeInputTargetsThePopup_data() {
  QTest::addColumn<int>("entry");
  QTest::newRow("popup") << 0;
  QTest::newRow("button-show-menu") << 1;
  QTest::newRow("toolbar-overflow-entry") << 2;
}

void TestButtonPopup::nativeInputTargetsThePopup() {
  if (!nativeFocusPlatform()) {
    QSKIP("Native IME focus requires the Windows or Cocoa platform plugin");
  }
  QFETCH(int, entry);
  PopupFixture fixture;
  if (entry == 2) {
    fixture.m_window.resize(260, 480);
  }
  QVERIFY(fixture.show());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(&fixture.m_editor));
  QSignalSpy editorChanges(fixture.m_editor.document(), &QTextDocument::contentsChanged);
  QSignalSpy submitted(fixture.m_input, &QLineEdit::returnPressed);
  QPointer<QToolButton> extension;
  QPointer<QMenu> overflow;
  if (entry == 2) {
    extension = fixture.m_toolbar.findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY(extension);
    QTRY_VERIFY(extension->isVisible());
    QTRY_VERIFY(!fixture.m_button->isVisible());
    overflow = extension->menu();
    QVERIFY(overflow);
    QVERIFY(overflow->actions().contains(fixture.m_tagAction));
  } else {
    QVERIFY(fixture.m_button->isVisible());
  }

  const auto chinese = QStringLiteral("\u4e2d\u6587\u6807\u7b7e");
  QString expected;
  for (int pass = 0; pass < 2; ++pass) {
    if (entry == 2) {
      QVERIFY(extension);
      overflow = extension->menu();
      QVERIFY(overflow);
    }
    bool finished = false;
    bool passed = false;
    bool timedOut = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, &fixture.m_window, [&]() {
      timedOut = true;
      fixture.m_popup->hide();
      if (overflow) {
        overflow->hide();
      }
    });
    QTimer drive;
    drive.setSingleShot(true);
    connect(&drive, &QTimer::timeout, &fixture.m_window, [&]() {
      const auto closeMenus = qScopeGuard([&]() {
        fixture.m_popup->hide();
        if (overflow) {
          overflow->hide();
        }
        finished = true;
      });
      if (overflow) {
        QVERIFY(overflow->isVisible());
        QVERIFY(overflow->actions().contains(fixture.m_tagAction));
        overflow->setActiveAction(fixture.m_tagAction);
        QTest::keyClick(overflow, Qt::Key_Right);
        QTRY_VERIFY(!overflow || !overflow->isVisible());
      }
      QTRY_VERIFY(fixture.m_popup->isVisible());
      // Do not type or save anything until Qt's platform-selected IME target
      // reaches the input, including any overflow-to-root handoff.
      QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
      QCOMPARE(fixture.m_popup->windowModality(), Qt::NonModal);
      QVERIFY(!QApplication::activeModalWidget());
      QCOMPARE(fixture.m_input->text(), expected);
      QCOMPARE(fixture.m_popup->parentWidget(), &fixture.m_toolbar);

      QPointer<QObject> receiver = QGuiApplication::focusObject();
      QCOMPARE(receiver.data(), static_cast<QObject *>(fixture.m_input));
      QInputMethodEvent preedit(chinese, {});
      QCoreApplication::sendEvent(receiver.data(), &preedit);
      QCOMPARE(fixture.m_input->text(), expected);
      QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
      QVERIFY(!fixture.m_editor.document()->isModified());

      EscapeClaim claim;
      fixture.m_input->installEventFilter(&claim);
      QTest::keyClick(fixture.m_input, Qt::Key_Escape);
      QVERIFY(claim.m_received);
      QVERIFY(fixture.m_popup->isVisible());
      fixture.m_input->removeEventFilter(&claim);
      // An unclaimed Escape during preedit must not dismiss the editor either.
      QTest::keyClick(fixture.m_input, Qt::Key_Escape);
      QVERIFY(fixture.m_popup->isVisible());
      receiver = QGuiApplication::focusObject();
      QCOMPARE(receiver.data(), static_cast<QObject *>(fixture.m_input));
      QInputMethodEvent cancel;
      QCoreApplication::sendEvent(receiver.data(), &cancel);
      QCOMPARE(fixture.m_input->text(), expected);
      QVERIFY(fixture.m_popup->isVisible());
      QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
      QVERIFY(!fixture.m_editor.document()->isModified());

      receiver = QGuiApplication::focusObject();
      QCOMPARE(receiver.data(), static_cast<QObject *>(fixture.m_input));
      QInputMethodEvent nextPreedit(chinese, {});
      QCoreApplication::sendEvent(receiver.data(), &nextPreedit);
      receiver = QGuiApplication::focusObject();
      QCOMPARE(receiver.data(), static_cast<QObject *>(fixture.m_input));
      QInputMethodEvent commit;
      commit.setCommitString(chinese);
      QCoreApplication::sendEvent(receiver.data(), &commit);
      expected += chinese;
      QCOMPARE(fixture.m_input->text(), expected);
      QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
      QVERIFY(!fixture.m_editor.document()->isModified());
      QCOMPARE(editorChanges.count(), 0);

      QTest::keyClick(fixture.m_input, Qt::Key_Return);
      QTest::keyClick(fixture.m_input, Qt::Key_Enter);
      QCOMPARE(submitted.count(), (pass + 1) * 2);
      QVERIFY(fixture.m_popup->isVisible());
      if (pass == 0) {
        QTest::keyClick(fixture.m_input, Qt::Key_Escape);
      } else {
        const auto outside = fixture.m_editor.viewport()->mapToGlobal(
            fixture.m_editor.viewport()->rect().bottomRight() - QPoint(10, 10));
        QVERIFY(!fixture.m_popup->frameGeometry().contains(outside));
        QTest::mouseClick(fixture.m_window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          fixture.m_window.mapFromGlobal(outside));
      }
      QTRY_VERIFY(!fixture.m_popup->isVisible());
      passed = true;
    });
    // showMenu() runs a nested event loop; both the driver and failure escape
    // hatch are scoped so no callback can outlive this opening or the fixture.
    watchdog.start(15000);
    drive.start(0);
    if (entry == 0) {
      fixture.m_popup->popup(fixture.anchor());
    } else if (entry == 1) {
      fixture.m_button->showMenu();
    } else {
      extension->showMenu();
    }
    QTRY_VERIFY(finished || timedOut);
    drive.stop();
    watchdog.stop();
    QVERIFY(!timedOut);
    QVERIFY(passed);
    QTRY_VERIFY(fixture.m_editor.hasFocus());
    QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
    QVERIFY(!fixture.m_editor.document()->isModified());
    QCOMPARE(editorChanges.count(), 0);
  }
}

void TestButtonPopup::ownedChildrenTakeEscapeFirst() {
  if (!nativeFocusPlatform()) {
    QSKIP("Native popup focus requires the Windows or Cocoa platform plugin");
  }
  PopupFixture fixture;
  QVERIFY(fixture.show());
  fixture.m_popup->popup(fixture.anchor());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));

  QMenu child(fixture.m_popup);
  child.addAction(QStringLiteral("Child command"));
  child.popup(fixture.m_input->mapToGlobal(QPoint(0, fixture.m_input->height())));
  QTRY_VERIFY(child.isVisible());
  QVERIFY(fixture.m_popup->isVisible());
  QTest::keyClick(&child, Qt::Key_Escape);
  QTRY_VERIFY(!child.isVisible());
  QVERIFY(fixture.m_popup->isVisible());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));

  QDialog dialog(fixture.m_popup);
  auto *layout = new QVBoxLayout(&dialog);
  auto *dialogInput = new QLineEdit(&dialog);
  layout->addWidget(dialogInput);
  dialog.open();
  QTRY_COMPARE(QApplication::activeModalWidget(), &dialog);
  QVERIFY(fixture.m_popup->isVisible());
  QTRY_VERIFY(dialogInput->hasFocus());
  QTest::keyClick(dialogInput, Qt::Key_Escape);
  QTRY_VERIFY(!dialog.isVisible());
  QVERIFY(fixture.m_popup->isVisible());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
  QTest::keyClick(fixture.m_input, Qt::Key_Escape);
  QTRY_VERIFY(!fixture.m_popup->isVisible());
  QTRY_VERIFY(fixture.m_editor.hasFocus());
  QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
  QVERIFY(!fixture.m_editor.document()->isModified());
}

void TestButtonPopup::pendingActivationSurvivesCloseAndOwnerDestruction() {
  if (!nativeFocusPlatform()) {
    QSKIP("Queued native activation requires the Windows or Cocoa platform plugin");
  }
  PopupFixture fixture;
  QVERIFY(fixture.show());
  fixture.m_popup->popup(fixture.anchor());
  fixture.m_popup->close();
  fixture.m_popup->popup(fixture.anchor());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
  QVERIFY(fixture.m_popup->isVisible());
  fixture.m_popup->close();
  QTRY_VERIFY(fixture.m_editor.hasFocus());

  auto doomed = std::make_unique<PopupFixture>();
  QVERIFY(doomed->show());
  QPointer<ButtonPopup> popup = doomed->m_popup;
  QPointer<QLineEdit> input = doomed->m_input;
  doomed->m_popup->popup(doomed->anchor());
  // No event dispatch between showing and destroying the QObject owner.
  doomed.reset();
  QVERIFY(!popup);
  QVERIFY(!input);
  fixture.m_window.raise();
  fixture.m_window.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&fixture.m_window));
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(&fixture.m_editor));
  fixture.m_popup->popup(fixture.anchor());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
  QTest::keyClick(fixture.m_input, Qt::Key_Escape);
  QTRY_VERIFY(!fixture.m_popup->isVisible());
  QTRY_VERIFY(fixture.m_editor.hasFocus());
  QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
  QVERIFY(!fixture.m_editor.document()->isModified());
}

void TestButtonPopup::aCancelledAncestorCannotStrandTheInput() {
  if (!nativeFocusPlatform()) {
    QSKIP("Native input panels require the Windows or Cocoa platform plugin");
  }
  PopupFixture fixture;
  QVERIFY(fixture.show());
  QMenu ancestor(&fixture.m_toolbar);
  ancestor.addAction(QStringLiteral("Parent menu"));
  ancestor.popup(fixture.anchor());
  QTRY_VERIFY(ancestor.isVisible());
  fixture.m_popup->popup(fixture.anchor());
  // Cancel before the queued popup-stack handoff. Do not leave an input-gated panel.
  ancestor.hide();
  QTRY_VERIFY(!fixture.m_popup->isVisible());
  fixture.m_popup->popup(fixture.anchor());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
  QTest::keyClick(fixture.m_input, Qt::Key_Escape);
  QTRY_VERIFY(!fixture.m_popup->isVisible());
}

void TestButtonPopup::aNativeContentWindowDismissesThePopup() {
  if (!nativeFocusPlatform()) {
    QSKIP("Native input panels require the Windows or Cocoa platform plugin");
  }
  PopupFixture fixture;
  QVERIFY(fixture.show());
  // Like a native-rendered content child, this QWindow has no QWidget peer.
  class ContentWindow : public QWindow {
  public:
    using QWindow::QWindow;
    int releases = 0;

  protected:
    void mouseReleaseEvent(QMouseEvent *) override { ++releases; }
  };
  ContentWindow content(fixture.m_window.windowHandle());
  content.setGeometry(20, 120, 200, 120);
  content.show();
  QVERIFY(QTest::qWaitForWindowExposed(&content));
  fixture.m_popup->popup(fixture.anchor());
  QTRY_COMPARE(QGuiApplication::focusObject(), static_cast<QObject *>(fixture.m_input));
  const auto release = qScopeGuard([&content]() {
    if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
      QTest::mouseRelease(&content, Qt::LeftButton, Qt::NoModifier, QPoint(100, 60));
    }
  });
  QTest::mousePress(&content, Qt::LeftButton, Qt::NoModifier, QPoint(100, 60));
  QTRY_VERIFY(!fixture.m_popup->isVisible());
  // A slow click must not outlive dismissal's idle-filter cleanup and leak mouse-up.
  QTest::qWait(QApplication::doubleClickInterval() + 50);
  QTest::mouseRelease(&content, Qt::LeftButton, Qt::NoModifier, QPoint(100, 60));
  QCOMPARE(content.releases, 0);
  QCOMPARE(fixture.m_editor.toPlainText(), fixture.m_original);
  QVERIFY(!fixture.m_editor.document()->isModified());
}

} // namespace tests

QTEST_MAIN(tests::TestButtonPopup)
#include "test_buttonpopup.moc"
