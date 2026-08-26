// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_commentpanel.cpp
//
// The comment dock is a pure view, but "pure view" is exactly where a
// deterministic data-loss bug hid: the panel debounces keystrokes, and if the
// pending edit is resolved against the CURRENT selection when the timer fires
// (rather than against the comment that was being typed into), then typing in
// A and clicking B silently discards A's text.
//
// These cases drive the provider's INTENT signals directly — the panel must
// never mutate anything itself — so they also pin the MVC boundary.

#include <QtTest>

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>

#include <core/servicelocator.h>
#include <core/services/commenttypes.h>
#include <gui/utils/commentcolorswatch.h>
#include <widgets/commentpanel.h>
#include <widgets/commentprovider.h>

using namespace vnotex;

namespace tests {

class TestCommentPanel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void listsEveryCommentAndFollowsTheSelection();
  void aHugeQuoteDoesNotBlowUpTheLayout();
  void colorBoxShowsCapitalizedNamesButStoresRawTokens();
  void typingEmitsOneIntentPerBurst();
  void switchingCommentsFlushesTheTypedTextToTheORIGINALComment();
  void switchingProvidersFlushesToTheOriginalProvider();
  void changingColorFlushesPendingText();
  void deletingDropsThePendingTextForThatComment();
  void aRepaintFromTheProviderDoesNotEchoAnIntent();
  void aNonEditableProviderDisablesEditing();
  void aNullProviderIsInert();

private:
  static Comment makeComment(const QString &p_id, int p_page, const QString &p_text);

  static QSharedPointer<CommentProvider> makeProvider(int p_count);

  static QPlainTextEdit *editorOf(CommentPanel *p_panel);

  static QListWidget *listOf(CommentPanel *p_panel);

  static QComboBox *colorBoxOf(CommentPanel *p_panel);

  static QPushButton *deleteButtonOf(CommentPanel *p_panel);

  ServiceLocator m_services;
};

void TestCommentPanel::initTestCase() {}

Comment TestCommentPanel::makeComment(const QString &p_id, int p_page, const QString &p_text) {
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{0, 0, 10, 0, 10, 10, 0, 10});
  Comment comment =
      Comment::create(PdfQuadsAnchor::make(p_page, quads, QStringLiteral("quoted %1").arg(p_page)),
                      p_text, CommentColor::defaultToken());
  comment.m_id = p_id;
  return comment;
}

QSharedPointer<CommentProvider> TestCommentPanel::makeProvider(int p_count) {
  auto provider = QSharedPointer<CommentProvider>::create();
  CommentSet set;
  for (int i = 0; i < p_count; ++i) {
    set.m_comments.append(makeComment(QStringLiteral("id-%1").arg(i), i, QString()));
  }
  provider->setComments(set);
  return provider;
}

// The panel builds its widgets in setupUI() and keeps them private; find them by
// type rather than exposing test-only accessors on production code.
QPlainTextEdit *TestCommentPanel::editorOf(CommentPanel *p_panel) {
  return p_panel->findChild<QPlainTextEdit *>();
}

QListWidget *TestCommentPanel::listOf(CommentPanel *p_panel) {
  return p_panel->findChild<QListWidget *>();
}

QComboBox *TestCommentPanel::colorBoxOf(CommentPanel *p_panel) {
  return p_panel->findChild<QComboBox *>();
}

QPushButton *TestCommentPanel::deleteButtonOf(CommentPanel *p_panel) {
  return p_panel->findChild<QPushButton *>();
}

void TestCommentPanel::listsEveryCommentAndFollowsTheSelection() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(3);
  panel.setCommentProvider(provider);

  auto *list = listOf(&panel);
  QVERIFY(list);
  QCOMPARE(list->count(), 3);

  provider->setSelectedId(QStringLiteral("id-1"));
  QCOMPARE(list->currentRow(), 1);
  QCOMPARE(editorOf(&panel)->toPlainText(), QString());

  // A selection that does not resolve clears the row rather than sticking.
  provider->setSelectedId(QStringLiteral("nope"));
  QCOMPARE(list->currentRow(), -1);
}

// The label is presentation; the DATA is the token that reaches comments.json.
// Conflating the two (the combo used to show the raw lowercase token) makes a
// translation able to change what is stored.
// A user highlighting half a page produced an anchor with thousands of
// characters. A word-wrapped QLabel reports a content-sized minimumSizeHint,
// which beats the layout's stretch factors, so the quote grew without bound and
// pushed the comment list off the dock entirely.
void TestCommentPanel::aHugeQuoteDoesNotBlowUpTheLayout() {
  CommentPanel panel(m_services);
  panel.resize(300, 600);

  // As long as a real selection can get: the adapter truncates at
  // maxAnchorTextLength(), so this is the worst case that can reach the dock.
  const QString huge = QStringLiteral("lorem ipsum dolor sit amet ")
                           .repeated(vnotex::PdfQuadsAnchor::maxAnchorTextLength() / 27 + 1)
                           .left(vnotex::PdfQuadsAnchor::maxAnchorTextLength());

  auto provider = QSharedPointer<CommentProvider>::create();
  CommentSet set;
  QVector<QVector<double>> quads;
  quads.append(QVector<double>{0, 0, 10, 0, 10, 10, 0, 10});
  auto comment = Comment::create(vnotex::PdfQuadsAnchor::make(0, quads, huge), QString(),
                                 vnotex::CommentColor::defaultToken());
  comment.m_id = QStringLiteral("id-huge");
  set.m_comments.append(comment);
  provider->setComments(set);

  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-huge"));
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));

  auto *anchor = panel.findChild<QLabel *>(QStringLiteral("CommentAnchorLabel"));
  QVERIFY(anchor);

  // The displayed quote is bounded...
  QVERIFY2(anchor->text().size() < 400,
           qPrintable(QStringLiteral("quote preview is %1 chars; it must be truncated")
                          .arg(anchor->text().size())));
  QVERIFY(anchor->text().endsWith(QStringLiteral("\u2026\u201D")));

  // ...nothing is lost...
  QCOMPARE(anchor->toolTip(), huge);

  // ...and, the actual bug: the label must not be allowed to eat the panel, so
  // the comment list keeps a usable share of the height.
  auto *list = listOf(&panel);
  QVERIFY(list);
  QVERIFY2(anchor->height() <= anchor->maximumHeight(), "the height cap is not being applied");
  QVERIFY2(anchor->height() < panel.height() / 3,
           qPrintable(QStringLiteral("quote label takes %1px of a %2px panel")
                          .arg(anchor->height())
                          .arg(panel.height())));
  QVERIFY2(list->height() > panel.height() / 4,
           qPrintable(QStringLiteral("comment list squeezed to %1px of a %2px panel")
                          .arg(list->height())
                          .arg(panel.height())));

  // The list row for the same comment is bounded too.
  QCOMPARE(list->count(), 1);
  QVERIFY(list->item(0)->text().size() < 400);
}

void TestCommentPanel::colorBoxShowsCapitalizedNamesButStoresRawTokens() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  auto *box = colorBoxOf(&panel);
  QVERIFY(box);

  const auto tokens = vnotex::CommentColor::all();
  QCOMPARE(box->count(), tokens.size());

  for (int i = 0; i < box->count(); ++i) {
    const QString label = box->itemText(i);
    const QString token = box->itemData(i).toString();

    QVERIFY2(
        tokens.contains(token),
        qPrintable(
            QStringLiteral("row %1 stores '%2', which is not a schema token").arg(i).arg(token)));
    QVERIFY2(!label.isEmpty() && label.at(0).isUpper(),
             qPrintable(
                 QStringLiteral("row %1 label '%2' must start with a capital").arg(i).arg(label)));
    QCOMPARE(label, vnotex::CommentColor::displayName(token));

    // The row now also carries a swatch. The fixture never calls
    // setSwatchResolver(), so it must match the UNTHEMED helper exactly -- and
    // adding an icon must not have changed what the row stores.
    QVERIFY2(!box->itemIcon(i).isNull(), qPrintable(QStringLiteral("row %1 has no swatch").arg(i)));
    QCOMPARE(box->itemIcon(i).pixmap(16, 16).toImage(),
             vnotex::CommentColorSwatch::icon(token).pixmap(16, 16).toImage());
  }

  // Picking a row must still emit the lowercase token, not the label.
  QSignalSpy colors(provider.data(), &CommentProvider::colorChangeRequested);
  const int idx = box->currentIndex() == 0 ? 1 : 0;
  const QString expected = box->itemData(idx).toString();
  box->setCurrentIndex(idx);

  QCOMPARE(colors.count(), 1);
  QCOMPARE(colors.at(0).at(1).toString(), expected);
  QVERIFY2(expected == expected.toLower(), "the stored token must stay lowercase");
}

void TestCommentPanel::typingEmitsOneIntentPerBurst() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(provider.data(), &CommentProvider::textEditRequested);

  auto *editor = editorOf(&panel);
  for (int i = 0; i < 10; ++i) {
    editor->setPlainText(QStringLiteral("draft %1").arg(i));
  }
  QCOMPARE(edits.count(), 0); // still inside the debounce window

  QTRY_COMPARE_WITH_TIMEOUT(edits.count(), 1, 3000);
  QCOMPARE(edits.at(0).at(0).toString(), QStringLiteral("id-0"));
  QCOMPARE(edits.at(0).at(1).toString(), QStringLiteral("draft 9"));
}

// The regression. Resolving the pending edit against the CURRENT selection when
// the timer fires loses A's text entirely.
void TestCommentPanel::switchingCommentsFlushesTheTypedTextToTheORIGINALComment() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(2);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(provider.data(), &CommentProvider::textEditRequested);

  editorOf(&panel)->setPlainText(QStringLiteral("belongs to A"));
  QCOMPARE(edits.count(), 0);

  // Select the other comment WELL inside the debounce window.
  provider->setSelectedId(QStringLiteral("id-1"));

  QVERIFY2(edits.count() >= 1, "the pending text was silently dropped");
  QCOMPARE(edits.at(0).at(0).toString(), QStringLiteral("id-0"));
  QCOMPARE(edits.at(0).at(1).toString(), QStringLiteral("belongs to A"));

  // ...and nothing was misattributed to B afterwards.
  QTest::qWait(600);
  for (int i = 0; i < edits.count(); ++i) {
    if (edits.at(i).at(0).toString() == QStringLiteral("id-1")) {
      QVERIFY2(edits.at(i).at(1).toString().isEmpty(), "A's text was written into B");
    }
  }
}

void TestCommentPanel::switchingProvidersFlushesToTheOriginalProvider() {
  CommentPanel panel(m_services);
  auto first = makeProvider(1);
  panel.setCommentProvider(first);
  first->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(first.data(), &CommentProvider::textEditRequested);
  editorOf(&panel)->setPlainText(QStringLiteral("belongs to the first file"));
  QCOMPARE(edits.count(), 0);

  // Another window becomes current mid-burst.
  auto second = makeProvider(1);
  panel.setCommentProvider(second);

  QCOMPARE(edits.count(), 1);
  QCOMPARE(edits.at(0).at(1).toString(), QStringLiteral("belongs to the first file"));

  // The second provider must not receive the first file's text.
  QSignalSpy secondEdits(second.data(), &CommentProvider::textEditRequested);
  QTest::qWait(600);
  QCOMPARE(secondEdits.count(), 0);
}

// A color change republishes the set, which repaints the editor; without a
// flush the repaint restores the last saved text over what is being typed.
void TestCommentPanel::changingColorFlushesPendingText() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(provider.data(), &CommentProvider::textEditRequested);
  QSignalSpy colors(provider.data(), &CommentProvider::colorChangeRequested);

  editorOf(&panel)->setPlainText(QStringLiteral("typed while picking a color"));

  auto *box = colorBoxOf(&panel);
  QVERIFY(box);
  const int otherIndex = box->currentIndex() == 0 ? 1 : 0;
  box->setCurrentIndex(otherIndex);

  QCOMPARE(colors.count(), 1);
  QVERIFY2(edits.count() >= 1, "the pending text was lost to the color change");
  QCOMPARE(edits.at(0).at(1).toString(), QStringLiteral("typed while picking a color"));
}

void TestCommentPanel::deletingDropsThePendingTextForThatComment() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(provider.data(), &CommentProvider::textEditRequested);
  QSignalSpy deletes(provider.data(), &CommentProvider::deleteRequested);

  editorOf(&panel)->setPlainText(QStringLiteral("about to be deleted"));
  deleteButtonOf(&panel)->click();

  QCOMPARE(deletes.count(), 1);
  // Writing to a comment that is about to cease to exist is a pointless round
  // trip, so the pending edit is DROPPED rather than flushed.
  QTest::qWait(600);
  QCOMPARE(edits.count(), 0);
}

// The panel repaints itself from the provider; a programmatic setPlainText must
// not be mistaken for user input and echoed straight back as an intent.
void TestCommentPanel::aRepaintFromTheProviderDoesNotEchoAnIntent() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);
  provider->setSelectedId(QStringLiteral("id-0"));

  QSignalSpy edits(provider.data(), &CommentProvider::textEditRequested);
  QSignalSpy colors(provider.data(), &CommentProvider::colorChangeRequested);

  CommentSet set;
  auto comment = makeComment(QStringLiteral("id-0"), 0, QStringLiteral("set from the controller"));
  comment.m_color = QStringLiteral("purple");
  set.m_comments.append(comment);
  provider->setComments(set);

  QTest::qWait(600);
  QCOMPARE(edits.count(), 0);
  QCOMPARE(colors.count(), 0);
  QCOMPARE(editorOf(&panel)->toPlainText(), QStringLiteral("set from the controller"));
}

void TestCommentPanel::aNonEditableProviderDisablesEditing() {
  CommentPanel panel(m_services);
  auto provider = makeProvider(1);
  panel.setCommentProvider(provider);

  QVERIFY(!editorOf(&panel)->isReadOnly());

  provider->setEditable(false);
  QVERIFY2(editorOf(&panel)->isReadOnly(), "a read-only store must not be typeable");
  QVERIFY(!colorBoxOf(&panel)->isEnabled());
  QVERIFY(!deleteButtonOf(&panel)->isEnabled());
}

// The dock is re-pointed on every currentViewWindowChanged, including at a
// window type with no comment support.
void TestCommentPanel::aNullProviderIsInert() {
  CommentPanel panel(m_services);
  panel.setCommentProvider(makeProvider(2));
  QCOMPARE(listOf(&panel)->count(), 2);

  panel.setCommentProvider(nullptr);
  QCOMPARE(listOf(&panel)->count(), 0);

  // No crash, no intent, nothing.
  deleteButtonOf(&panel)->click();
  editorOf(&panel)->setPlainText(QStringLiteral("typing into nothing"));
  QTest::qWait(600);
}

} // namespace tests

QTEST_MAIN(tests::TestCommentPanel)
#include "test_commentpanel.moc"
