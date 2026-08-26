// PdfAnnotationToolBar: the three annotation buttons and their per-tool
// settings menus, on a bare QToolBar.
//
// Without this gate an implementation could ship InstantPopup, a menu on the
// wrong button, a hidden indicator, or a live menu on a read-only file, and
// every other test in the feature would still pass.

#include <QtTest>

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QToolBar>
#include <QToolButton>

#include <core/pdfviewerconfig.h>
#include <core/services/commenttypes.h>
#include <gui/utils/commentcolorswatch.h>
#include <widgets/pdfannotationtoolbar.h>

using namespace vnotex;

namespace tests {

namespace {

QAction *menuActionWithData(QMenu *p_menu, const QVariant &p_data) {
  for (auto *act : p_menu->actions()) {
    if (act->data() == p_data) {
      return act;
    }
  }
  return nullptr;
}

int checkedCount(const QList<QAction *> &p_actions) {
  int count = 0;
  for (auto *act : p_actions) {
    if (act->isChecked()) {
      ++count;
    }
  }
  return count;
}

QList<QAction *> checkableMenuActions(QMenu *p_menu) {
  QList<QAction *> actions;
  for (auto *act : p_menu->actions()) {
    if (act->isCheckable()) {
      actions.append(act);
    }
  }
  return actions;
}

} // namespace

class TestPdfAnnotationToolBar : public QObject {
  Q_OBJECT

private slots:
  void thereAreExactlyThreeToolButtonsAndNoColourButton();

  void everyToolButtonIsAMenuButtonPopup();

  void theToolGroupIsNonExclusiveSoTheArmedToolCanBeDisarmed();

  void eachMenuOffersEveryColourAndTheRightScalars();

  void colourAndScalarGroupsAreIndependentlyExclusive();

  void syncStateMovesTheTicks();

  void disablingAuthoringDisablesTheMenusToo();

  void menuColoursCarrySwatchIcons();
};

void TestPdfAnnotationToolBar::thereAreExactlyThreeToolButtonsAndNoColourButton() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QCOMPARE(bar.toolActions().size(), 3);
  // The toolbar carries the three tools and NOTHING else: the standalone colour
  // button this change removed would show up here.
  QCOMPARE(toolBar.actions().size(), 3);

  for (const auto &tool : PdfViewerConfig::toolNames()) {
    QVERIFY2(bar.toolAction(tool), qPrintable(tool));
    QVERIFY2(bar.toolAction(tool)->isCheckable(), qPrintable(tool));
  }
}

void TestPdfAnnotationToolBar::everyToolButtonIsAMenuButtonPopup() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  for (const auto &tool : PdfViewerConfig::toolNames()) {
    auto *button = bar.toolButton(tool);
    QVERIFY2(button, qPrintable(tool));
    // The split is the whole point: body arms, indicator opens the settings.
    QCOMPARE(button->popupMode(), QToolButton::MenuButtonPopup);
    QVERIFY2(button->menu(), qPrintable(tool));
    QCOMPARE(button->menu(), bar.toolMenu(tool));
    // Setting NoMenuIndicator would make every theme hide the affordance this
    // component exists to provide.
    QVERIFY2(!button->property("NoMenuIndicator").toBool(), qPrintable(tool));
  }

  // Each tool got its OWN menu, not a shared one on the wrong button.
  QVERIFY(bar.toolMenu(PdfToolOptions::highlightTool()) != bar.toolMenu(PdfToolOptions::inkTool()));
}

void TestPdfAnnotationToolBar::theToolGroupIsNonExclusiveSoTheArmedToolCanBeDisarmed() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QVERIFY(!bar.isToolGroupExclusive());

  QSignalSpy toggles(&bar, &PdfAnnotationToolBar::toolToggled);
  auto *ink = bar.toolAction(PdfToolOptions::inkTool());

  ink->trigger();
  QCOMPARE(toggles.count(), 1);
  QCOMPARE(toggles.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(toggles.last().at(1).toBool(), true);

  // Clicking the ARMED tool must disarm it, which an exclusive group forbids.
  ink->trigger();
  QCOMPARE(toggles.count(), 2);
  QCOMPARE(toggles.last().at(1).toBool(), false);
}

void TestPdfAnnotationToolBar::eachMenuOffersEveryColourAndTheRightScalars() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);
  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);

  for (const auto &tool : PdfViewerConfig::toolNames()) {
    auto *menu = bar.toolMenu(tool);
    QVERIFY2(menu, qPrintable(tool));

    // Driven by the schema, so a token added to CommentColor::all() shows up in
    // every picker at once.
    for (const auto &token : CommentColor::all()) {
      auto *act = menuActionWithData(menu, token);
      QVERIFY2(act, qPrintable(tool + QLatin1Char('/') + token));
      QVERIFY(act->isCheckable());
    }

    const int expectedScalars =
        (PdfToolOptions::hasWidth(tool) || PdfToolOptions::hasFontSize(tool)) ? 3 : 0;
    QCOMPARE(checkableMenuActions(menu).size(), CommentColor::all().size() + expectedScalars);
  }

  // Picking carries the tool AND the raw token.
  menuActionWithData(bar.toolMenu(PdfToolOptions::inkTool()), QStringLiteral("blue"))->trigger();
  QCOMPARE(colors.count(), 1);
  QCOMPARE(colors.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(colors.last().at(1).toString(), QStringLiteral("blue"));

  // Ink offers widths inside PdfInkAnchor's bounds; free text offers sizes
  // inside PdfFreeTextAnchor's.
  auto *thick = menuActionWithData(bar.toolMenu(PdfToolOptions::inkTool()), 3.0);
  QVERIFY(thick);
  thick->trigger();
  QCOMPARE(scalars.count(), 1);
  QCOMPARE(scalars.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(scalars.last().at(1).toDouble(), 3.0);

  auto *large = menuActionWithData(bar.toolMenu(PdfToolOptions::freeTextTool()), 16.0);
  QVERIFY(large);
  large->trigger();
  QCOMPARE(scalars.count(), 2);
  QCOMPARE(scalars.last().at(0).toString(), PdfToolOptions::freeTextTool());
  QCOMPARE(scalars.last().at(1).toDouble(), 16.0);

  // The EXACT menus, in order. Asserting only "three entries, all in bounds"
  // would pass for Thin/Medium/Thick 1/2/3, which is not what was specified.
  const auto scalarsOf = [&bar](const QString &p_tool) {
    QList<double> values;
    for (auto *act : bar.toolMenu(p_tool)->actions()) {
      if (act->isCheckable() && act->data().typeId() == QMetaType::Double) {
        values.append(act->data().toDouble());
      }
    }
    return values;
  };
  QCOMPARE(scalarsOf(PdfToolOptions::inkTool()), (QList<double>{0.75, 1.5, 3.0}));
  QCOMPARE(scalarsOf(PdfToolOptions::freeTextTool()), (QList<double>{9.0, 12.0, 16.0}));

  // And each stays inside the anchor validators' bounds, so a menu pick can
  // never produce an anchor those validators would reject.
  for (double value : scalarsOf(PdfToolOptions::inkTool())) {
    QVERIFY(value >= PdfInkAnchor::minWidth() && value <= PdfInkAnchor::maxWidth());
  }
  for (double value : scalarsOf(PdfToolOptions::freeTextTool())) {
    QVERIFY(value >= PdfFreeTextAnchor::minFontSize() && value <= PdfFreeTextAnchor::maxFontSize());
  }

  // The default width/font size must be OFFERABLE, or a fresh config shows no
  // tick at all.
  QVERIFY(scalarsOf(PdfToolOptions::inkTool()).contains(PdfToolOptions::defaultWidth()));
  QVERIFY(scalarsOf(PdfToolOptions::freeTextTool()).contains(PdfToolOptions::defaultFontSize()));
}

void TestPdfAnnotationToolBar::colourAndScalarGroupsAreIndependentlyExclusive() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QHash<QString, PdfViewerConfig::ToolOptions> options;
  PdfViewerConfig::ToolOptions ink;
  ink.m_color = QStringLiteral("green");
  ink.m_width = 3.0;
  options.insert(PdfToolOptions::inkTool(), ink);
  bar.syncState(PdfToolOptions::inkTool(), options);

  auto *menu = bar.toolMenu(PdfToolOptions::inkTool());
  QVERIFY(menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  QVERIFY(menuActionWithData(menu, 3.0)->isChecked());

  // Picking a WIDTH must not clear the colour tick, and vice versa. Checking is
  // what QActionGroup governs (QAction::trigger() does not toggle; the QMenu
  // sets the state and then triggers), so drive it the same way the menu does.
  menuActionWithData(menu, 0.75)->setChecked(true);
  menuActionWithData(menu, 0.75)->trigger();
  QVERIFY(menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  QVERIFY(!menuActionWithData(menu, 3.0)->isChecked());
  QVERIFY(menuActionWithData(menu, 0.75)->isChecked());

  menuActionWithData(menu, QStringLiteral("pink"))->setChecked(true);
  menuActionWithData(menu, QStringLiteral("pink"))->trigger();
  QVERIFY(menuActionWithData(menu, 0.75)->isChecked());
  QVERIFY(!menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  // Exactly one tick per group, which is what "independently exclusive" means.
  QCOMPARE(checkedCount(checkableMenuActions(menu)), 2);
}

void TestPdfAnnotationToolBar::syncStateMovesTheTicks() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy toggles(&bar, &PdfAnnotationToolBar::toolToggled);
  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);

  QHash<QString, PdfViewerConfig::ToolOptions> options;
  PdfViewerConfig::ToolOptions freetext;
  freetext.m_color = QStringLiteral("purple");
  freetext.m_fontSize = 9.0;
  options.insert(PdfToolOptions::freeTextTool(), freetext);

  bar.syncState(PdfToolOptions::freeTextTool(), options);

  QVERIFY(bar.toolAction(PdfToolOptions::freeTextTool())->isChecked());
  QVERIFY(!bar.toolAction(PdfToolOptions::inkTool())->isChecked());
  QVERIFY(!bar.toolAction(PdfToolOptions::highlightTool())->isChecked());

  auto *menu = bar.toolMenu(PdfToolOptions::freeTextTool());
  QVERIFY(menuActionWithData(menu, QStringLiteral("purple"))->isChecked());
  QVERIFY(menuActionWithData(menu, 9.0)->isChecked());
  QCOMPARE(checkedCount(checkableMenuActions(menu)), 2);

  // The web side can disarm a tool by itself, so syncState must be able to
  // clear every tick too.
  bar.syncState(QStringLiteral("none"), options);
  QVERIFY(!bar.toolAction(PdfToolOptions::freeTextTool())->isChecked());

  // Repainting is not a user action: nothing may echo back out.
  QCOMPARE(toggles.count(), 0);
  QCOMPARE(colors.count(), 0);
}

void TestPdfAnnotationToolBar::disablingAuthoringDisablesTheMenusToo() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy toggles(&bar, &PdfAnnotationToolBar::toolToggled);
  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);
  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);

  bar.setAuthoringEnabled(false);

  for (const auto &tool : PdfViewerConfig::toolNames()) {
    QVERIFY2(!bar.toolAction(tool)->isEnabled(), qPrintable(tool));
    QVERIFY2(!bar.toolButton(tool)->isEnabled(), qPrintable(tool));
    QVERIFY2(!bar.toolMenu(tool)->isEnabled(), qPrintable(tool));

    // A read-only file must be un-annotatable, not merely refused after the
    // fact. QAction::trigger() consults only the action's OWN enabled state, so
    // disabling the menu alone would leave every row live.
    bar.toolAction(tool)->trigger();
    for (auto *act : checkableMenuActions(bar.toolMenu(tool))) {
      QVERIFY2(!act->isEnabled(), qPrintable(tool));
      act->trigger();
    }
  }

  QCOMPARE(toggles.count(), 0);
  QCOMPARE(colors.count(), 0);
  QCOMPARE(scalars.count(), 0);

  // And it comes back.
  bar.setAuthoringEnabled(true);
  bar.toolAction(PdfToolOptions::inkTool())->trigger();
  QCOMPARE(toggles.count(), 1);
}

void TestPdfAnnotationToolBar::menuColoursCarrySwatchIcons() {
  QToolBar toolBar;
  // Default-constructed resolver == the built-in colours, which is what an
  // unthemed caller gets.
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  auto *menu = bar.toolMenu(PdfToolOptions::highlightTool());
  for (const auto &token : CommentColor::all()) {
    auto *act = menuActionWithData(menu, token);
    QVERIFY2(!act->icon().isNull(), qPrintable(token));
    // The LABEL is the translated display name and the DATA is the raw token;
    // an icon must never change either.
    QCOMPARE(act->text(), CommentColor::displayName(token));
    QCOMPARE(act->data().toString(), token);
  }

  // A theme switch re-supplies both the resolver and the border, then rebuilds.
  const auto before =
      menuActionWithData(menu, QStringLiteral("yellow"))->icon().pixmap(16, 16).toImage();
  bar.setSwatchResolver([](const QString &) { return QStringLiteral("rgb(0, 0, 255)"); },
                        QStringLiteral("#000000"));
  const auto after =
      menuActionWithData(menu, QStringLiteral("yellow"))->icon().pixmap(16, 16).toImage();
  QVERIFY(before != after);
}

} // namespace tests

QTEST_MAIN(tests::TestPdfAnnotationToolBar)
#include "test_pdfannotationtoolbar.moc"
