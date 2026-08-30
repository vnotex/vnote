// PdfAnnotationToolBar: the three annotation buttons and their per-tool
// settings menus, on a bare QToolBar.
//
// Without this gate an implementation could ship InstantPopup, a menu on the
// wrong button, a hidden indicator, or a live menu on a read-only file, and
// every other test in the feature would still pass.

#include <QtTest>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

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

  void eachMenuOffersEveryColourAndTheRightSliders();

  void thePickerGroupsAreIndependent();

  void syncStateMovesTheTicksAndTheSlidersWithoutEchoing();

  void anOutOfSliderRangeStoredValueIsClampedForDisplayOnly();

  void disablingAuthoringDisablesTheMenusToo();

  void menuColoursCarrySwatchIcons();

  void theBrokenThemeCheckRingIsSuppressed();
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

void TestPdfAnnotationToolBar::eachMenuOffersEveryColourAndTheRightSliders() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);
  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);
  QSignalSpy opacities(&bar, &PdfAnnotationToolBar::opacityPicked);

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

    // Colour is the ONLY checkable row now: the scalar presets became sliders.
    QCOMPARE(checkableMenuActions(menu).size(), CommentColor::all().size());

    // And exactly the slider rows the tool is supposed to carry, no more.
    int sliderRows = 0;
    for (auto *act : menu->actions()) {
      if (auto *widgetAct = qobject_cast<QWidgetAction *>(act)) {
        sliderRows += widgetAct->defaultWidget()->findChildren<QSlider *>().size();
      }
    }
    const int expected = tool == PdfToolOptions::inkTool()        ? 2
                         : tool == PdfToolOptions::freeTextTool() ? 1
                                                                  : 0;
    QCOMPARE(sliderRows, expected);
  }

  // Picking carries the tool AND the raw token.
  menuActionWithData(bar.toolMenu(PdfToolOptions::inkTool()), QStringLiteral("blue"))->trigger();
  QCOMPARE(colors.count(), 1);
  QCOMPARE(colors.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(colors.last().at(1).toString(), QStringLiteral("blue"));

  // Draw carries Thickness AND Opacity; Text box only Font size; Highlight
  // neither.
  auto *thickness = bar.scalarSlider(PdfToolOptions::inkTool());
  QVERIFY(thickness);
  QCOMPARE(thickness->minimum(), 1);
  QCOMPARE(thickness->maximum(), 240);

  auto *opacity = bar.opacitySlider(PdfToolOptions::inkTool());
  QVERIFY(opacity);
  QCOMPARE(opacity->minimum(), 10);
  QCOMPARE(opacity->maximum(), 100);

  auto *fontSize = bar.scalarSlider(PdfToolOptions::freeTextTool());
  QVERIFY(fontSize);
  QCOMPARE(fontSize->minimum(), 6);
  QCOMPARE(fontSize->maximum(), 72);
  // Opacity is Draw-only: a Text box or Highlight opacity slider would be a
  // feature nobody asked for and a schema key nothing reads.
  QVERIFY(!bar.opacitySlider(PdfToolOptions::freeTextTool()));
  QVERIFY(!bar.opacitySlider(PdfToolOptions::highlightTool()));
  QVERIFY(!bar.scalarSlider(PdfToolOptions::highlightTool()));

  // Each row is a QWidgetAction, NOT a plain action: a plain one would close the
  // menu on the first click and make the slider undraggable.
  QVERIFY(bar.scalarAction(PdfToolOptions::inkTool()));
  QVERIFY(bar.opacityAction(PdfToolOptions::inkTool()));
  QVERIFY(bar.scalarAction(PdfToolOptions::inkTool())->defaultWidget());

  // Moving a slider emits ONCE, with the mapped double.
  thickness->setValue(30);
  QCOMPARE(scalars.count(), 1);
  QCOMPARE(scalars.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(scalars.last().at(1).toDouble(), 3.0);

  fontSize->setValue(16);
  QCOMPARE(scalars.count(), 2);
  QCOMPARE(scalars.last().at(0).toString(), PdfToolOptions::freeTextTool());
  QCOMPARE(scalars.last().at(1).toDouble(), 16.0);

  opacity->setValue(35);
  QCOMPARE(opacities.count(), 1);
  QCOMPARE(opacities.last().at(0).toString(), PdfToolOptions::inkTool());
  QCOMPARE(opacities.last().at(1).toDouble(), 0.35);

  // Every reachable slider position stays inside the anchor validators' bounds,
  // so a slider pick can never produce an anchor those validators would reject.
  for (int v : {thickness->minimum(), thickness->maximum()}) {
    const double width = PdfAnnotationToolBar::scalarFromSlider(PdfToolOptions::inkTool(), v);
    QVERIFY(width >= PdfInkAnchor::minWidth() && width <= PdfInkAnchor::maxWidth());
  }
  for (int v : {fontSize->minimum(), fontSize->maximum()}) {
    const double size = PdfAnnotationToolBar::scalarFromSlider(PdfToolOptions::freeTextTool(), v);
    QVERIFY(size >= PdfFreeTextAnchor::minFontSize() && size <= PdfFreeTextAnchor::maxFontSize());
  }
  for (int v : {opacity->minimum(), opacity->maximum()}) {
    const double o = PdfAnnotationToolBar::opacityFromSlider(v);
    QVERIFY(o >= PdfInkAnchor::minOpacity() && o <= PdfInkAnchor::maxOpacity());
  }
}

void TestPdfAnnotationToolBar::thePickerGroupsAreIndependent() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QHash<QString, PdfViewerConfig::ToolOptions> options;
  PdfViewerConfig::ToolOptions ink;
  ink.m_color = QStringLiteral("green");
  ink.m_width = 3.0;
  ink.m_opacity = 0.5;
  options.insert(PdfToolOptions::inkTool(), ink);
  bar.syncState(PdfToolOptions::inkTool(), options);

  auto *menu = bar.toolMenu(PdfToolOptions::inkTool());
  QVERIFY(menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  QCOMPARE(bar.scalarSlider(PdfToolOptions::inkTool())->value(), 30);
  QCOMPARE(bar.opacitySlider(PdfToolOptions::inkTool())->value(), 50);

  // Moving the thickness slider must not clear the colour tick, and picking a
  // colour must not move a slider.
  bar.scalarSlider(PdfToolOptions::inkTool())->setValue(8);
  QVERIFY(menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  QCOMPARE(bar.opacitySlider(PdfToolOptions::inkTool())->value(), 50);

  menuActionWithData(menu, QStringLiteral("pink"))->setChecked(true);
  menuActionWithData(menu, QStringLiteral("pink"))->trigger();
  QVERIFY(!menuActionWithData(menu, QStringLiteral("green"))->isChecked());
  QCOMPARE(bar.scalarSlider(PdfToolOptions::inkTool())->value(), 8);
  // Exactly one colour tick, which is what "exclusive colour group" means.
  QCOMPARE(checkedCount(checkableMenuActions(menu)), 1);
}

void TestPdfAnnotationToolBar::syncStateMovesTheTicksAndTheSlidersWithoutEchoing() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy toggles(&bar, &PdfAnnotationToolBar::toolToggled);
  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);
  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);
  QSignalSpy opacities(&bar, &PdfAnnotationToolBar::opacityPicked);

  QHash<QString, PdfViewerConfig::ToolOptions> options;
  PdfViewerConfig::ToolOptions freetext;
  freetext.m_color = QStringLiteral("purple");
  freetext.m_fontSize = 9.0;
  options.insert(PdfToolOptions::freeTextTool(), freetext);
  PdfViewerConfig::ToolOptions ink;
  ink.m_width = 2.5;
  ink.m_opacity = 0.4;
  options.insert(PdfToolOptions::inkTool(), ink);

  bar.syncState(PdfToolOptions::freeTextTool(), options);

  QVERIFY(bar.toolAction(PdfToolOptions::freeTextTool())->isChecked());
  QVERIFY(!bar.toolAction(PdfToolOptions::inkTool())->isChecked());
  QVERIFY(!bar.toolAction(PdfToolOptions::highlightTool())->isChecked());

  auto *menu = bar.toolMenu(PdfToolOptions::freeTextTool());
  QVERIFY(menuActionWithData(menu, QStringLiteral("purple"))->isChecked());
  QCOMPARE(checkedCount(checkableMenuActions(menu)), 1);
  QCOMPARE(bar.scalarSlider(PdfToolOptions::freeTextTool())->value(), 9);
  QCOMPARE(bar.scalarSlider(PdfToolOptions::inkTool())->value(), 25);
  QCOMPARE(bar.opacitySlider(PdfToolOptions::inkTool())->value(), 40);

  // The web side can disarm a tool by itself, so syncState must be able to
  // clear every tick too.
  bar.syncState(QStringLiteral("none"), options);
  QVERIFY(!bar.toolAction(PdfToolOptions::freeTextTool())->isChecked());

  // Repainting is not a user action: nothing may echo back out. QSlider::setValue
  // DOES emit valueChanged, so without the QSignalBlocker this loops.
  QCOMPARE(toggles.count(), 0);
  QCOMPARE(colors.count(), 0);
  QCOMPARE(scalars.count(), 0);
  QCOMPARE(opacities.count(), 0);
}

// The slider ranges are deliberately NARROWER than the schema ranges (width 24
// vs 64, font size 6-72 vs 4-144). A hand-edited config value outside them must
// be clamped for DISPLAY only -- silently rewriting the user's 40pt pen on first
// paint would be data loss.
void TestPdfAnnotationToolBar::anOutOfSliderRangeStoredValueIsClampedForDisplayOnly() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);
  QSignalSpy opacities(&bar, &PdfAnnotationToolBar::opacityPicked);

  QHash<QString, PdfViewerConfig::ToolOptions> options;
  PdfViewerConfig::ToolOptions ink;
  ink.m_width = 40.0;
  options.insert(PdfToolOptions::inkTool(), ink);
  PdfViewerConfig::ToolOptions small;
  small.m_fontSize = 4.0;
  options.insert(PdfToolOptions::freeTextTool(), small);

  bar.syncState(PdfToolOptions::inkTool(), options);
  QCOMPARE(bar.scalarSlider(PdfToolOptions::inkTool())->value(), 240);
  QCOMPARE(bar.scalarSlider(PdfToolOptions::freeTextTool())->value(), 6);

  // The VALUE LABEL is clamped too. A thumb pinned at the top beside a label
  // reading "40.0" would be a worse lie than either alone.
  const auto labelOf = [](QWidgetAction *p_action) {
    const auto labels = p_action->defaultWidget()->findChildren<QLabel *>();
    return labels.isEmpty() ? QString() : labels.last()->text();
  };
  QCOMPARE(labelOf(bar.scalarAction(PdfToolOptions::inkTool())), QStringLiteral("24.0"));
  QCOMPARE(labelOf(bar.scalarAction(PdfToolOptions::freeTextTool())), QStringLiteral("6"));

  PdfViewerConfig::ToolOptions large;
  large.m_fontSize = 144.0;
  options.insert(PdfToolOptions::freeTextTool(), large);
  bar.syncState(PdfToolOptions::inkTool(), options);
  QCOMPARE(bar.scalarSlider(PdfToolOptions::freeTextTool())->value(), 72);

  // Clamping is display-only: nothing is emitted, so nothing is written back.
  QCOMPARE(scalars.count(), 0);
  QCOMPARE(opacities.count(), 0);
}

void TestPdfAnnotationToolBar::disablingAuthoringDisablesTheMenusToo() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  QSignalSpy toggles(&bar, &PdfAnnotationToolBar::toolToggled);
  QSignalSpy colors(&bar, &PdfAnnotationToolBar::colorPicked);
  QSignalSpy scalars(&bar, &PdfAnnotationToolBar::scalarPicked);
  QSignalSpy opacities(&bar, &PdfAnnotationToolBar::opacityPicked);

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

    // The QWidgetAction, its row widget AND the slider: an enabled child widget
    // inside a disabled action is still interactive in some styles.
    if (auto *act = bar.scalarAction(tool)) {
      QVERIFY2(!act->isEnabled(), qPrintable(tool));
      QVERIFY2(!act->defaultWidget()->isEnabled(), qPrintable(tool));
      QVERIFY2(!bar.scalarSlider(tool)->isEnabled(), qPrintable(tool));
    }
    if (auto *act = bar.opacityAction(tool)) {
      QVERIFY2(!act->isEnabled(), qPrintable(tool));
      QVERIFY2(!act->defaultWidget()->isEnabled(), qPrintable(tool));
      QVERIFY2(!bar.opacitySlider(tool)->isEnabled(), qPrintable(tool));
    }
  }

  QCOMPARE(toggles.count(), 0);
  QCOMPARE(colors.count(), 0);
  QCOMPARE(scalars.count(), 0);
  QCOMPARE(opacities.count(), 0);

  // And it comes back.
  bar.setAuthoringEnabled(true);
  QVERIFY(bar.scalarSlider(PdfToolOptions::inkTool())->isEnabled());
  QVERIFY(bar.opacitySlider(PdfToolOptions::inkTool())->isEnabled());
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

// Every interface.qss marks a checked icon-bearing action with
// `QMenu::icon:checked { border: 2px solid @widgets#qmenu#fg; }`. Qt draws that
// around the icon SUB-CONTROL rect and clips it to a PARTIAL box at fractional
// device pixel ratios -- at 1.5 only the top and bottom edges survive, which
// looks like a rendering fault. The swatch carries its own tick instead, so the
// ring has to be suppressed or the broken one comes back.
void TestPdfAnnotationToolBar::theBrokenThemeCheckRingIsSuppressed() {
  QToolBar toolBar;
  PdfAnnotationToolBar bar;
  bar.install(&toolBar);

  for (const auto &tool : PdfViewerConfig::toolNames()) {
    auto *menu = bar.toolMenu(tool);
    QVERIFY2(menu, qPrintable(tool));

    const auto sheet = menu->styleSheet().simplified().remove(QLatin1Char(' '));
    QVERIFY2(sheet.contains(QStringLiteral("QMenu::icon:checked{border:none;}")),
             qPrintable(QStringLiteral("%1 menu does not neutralise the themed check ring: '%2'")
                            .arg(tool, menu->styleSheet())));
  }
}

} // namespace tests

QTEST_MAIN(tests::TestPdfAnnotationToolBar)
#include "test_pdfannotationtoolbar.moc"
