#include <QAction>
#include <QMenu>
#include <QMetaObject>
#include <QSignalSpy>
#include <QtTest>

#include <widgets/encodingbutton.h>

using namespace vnotex;

namespace tests {

// Pure-view coverage for EncodingButton: label reflects the current encoding,
// the menu is populated lazily, and selecting an entry emits the intent signal.
class TestEncodingButton : public QObject {
  Q_OBJECT

private slots:
  void testDefaultLabelIsUtf8();
  void testSetCurrentEncodingUpdatesLabel();
  void testEmptyResetsToUtf8();
  void testMenuBuiltLazily();
  void testSelectingEntryEmitsRequest();

private:
  // Force the lazily-built menu to populate (production builds it on the
  // menu's aboutToShow). Emitting the signal via the meta-object drives the
  // connected buildMenu slot deterministically without a real popup.
  static void populateMenu(EncodingButton &p_button) {
    QMetaObject::invokeMethod(p_button.menu(), "aboutToShow");
  }
};

void TestEncodingButton::testDefaultLabelIsUtf8() {
  EncodingButton button;
  QCOMPARE(button.text(), QStringLiteral("UTF-8"));
}

void TestEncodingButton::testSetCurrentEncodingUpdatesLabel() {
  EncodingButton button;
  button.setCurrentEncoding(QStringLiteral("GB18030"));
  QCOMPARE(button.text(), QStringLiteral("GB18030"));
}

void TestEncodingButton::testEmptyResetsToUtf8() {
  EncodingButton button;
  button.setCurrentEncoding(QStringLiteral("GB18030"));
  button.setCurrentEncoding(QString());
  QCOMPARE(button.text(), QStringLiteral("UTF-8"));
}

void TestEncodingButton::testMenuBuiltLazily() {
  EncodingButton button;
  QVERIFY(button.menu() != nullptr);
  // Not populated until the menu is about to show.
  QVERIFY(button.menu()->actions().isEmpty());

  populateMenu(button);
  QVERIFY(!button.menu()->actions().isEmpty());

  // Idempotent: a second aboutToShow must not duplicate entries.
  const int count = button.menu()->actions().size();
  populateMenu(button);
  QCOMPARE(button.menu()->actions().size(), count);
}

void TestEncodingButton::testSelectingEntryEmitsRequest() {
  EncodingButton button;
  QSignalSpy spy(&button, &EncodingButton::encodingChangeRequested);

  populateMenu(button);

  QAction *gbAction = nullptr;
  const auto actions = button.menu()->actions();
  for (QAction *act : actions) {
    if (act->text() == QStringLiteral("GB18030")) {
      gbAction = act;
      break;
    }
  }
  QVERIFY2(gbAction != nullptr, "GB18030 should be in the curated shortlist");

  gbAction->trigger();

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("GB18030"));
}

} // namespace tests

QTEST_MAIN(tests::TestEncodingButton)
#include "test_encoding_button.moc"
