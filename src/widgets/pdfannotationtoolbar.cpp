#include "pdfannotationtoolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include <core/services/commenttypes.h>

#include "propertydefs.h"

using namespace vnotex;

namespace {

// Size of the colour chip painted into every menu row.
constexpr int c_swatchSizePx = 16;

// Slider coordinates. Integer sliders, so each row scales its double value.
//
// The ink range tops out at 24.0 PDF units, well below PdfInkAnchor::maxWidth()
// (64): a 64pt pen is unusable, and a hand-edited config can still express one
// -- syncState clamps such a value for DISPLAY only and never writes it back.
// The font-size range is likewise narrower than PdfFreeTextAnchor's 4..144.
constexpr int c_inkWidthSliderMin = 1;
constexpr int c_inkWidthSliderMax = 240;
constexpr double c_inkWidthSliderScale = 10.0;

constexpr int c_fontSizeSliderMin = 6;
constexpr int c_fontSizeSliderMax = 72;

constexpr int c_opacitySliderMin = 10;
constexpr int c_opacitySliderMax = 100;
constexpr double c_opacitySliderScale = 100.0;

} // namespace

PdfAnnotationToolBar::PdfAnnotationToolBar(CommentColorSwatch::ColorResolver p_resolve,
                                           QString p_borderCss, QObject *p_parent)
    : QObject(p_parent), m_resolve(std::move(p_resolve)), m_borderCss(std::move(p_borderCss)) {}

void PdfAnnotationToolBar::install(QToolBar *p_toolBar, const IconProvider &p_icons) {
  Q_ASSERT(p_toolBar);
  Q_ASSERT(m_tools.isEmpty());

  m_toolGroup = new QActionGroup(this);
  // Non-exclusive: clicking the armed tool again must DISARM it, which an
  // exclusive group forbids (it keeps one member checked forever).
  m_toolGroup->setExclusive(false);

  addTool(p_toolBar, PdfToolOptions::highlightTool(), QStringLiteral("type_mark_editor.svg"),
          tr("Highlight"), p_icons);
  // NOT edit_editor.svg: that pencil is the Edit/Read toggle sitting in this
  // same toolbar (viewwindowtoolbarhelper2.cpp), and two identical icons a few
  // pixels apart is worse than no icon.
  //
  // Stock Lucide icons, matching the rest of the icon set (edit_editor.svg is
  // lucide-pencil). Normalized to an explicit stroke="#000000" rather than
  // Lucide's "currentColor", because IconUtils::fetchIcon recolors by rewriting
  // the stroke attribute and Qt's SVG renderer does not resolve currentColor
  // reliably.
  //
  // Draw is lucide-signature, NOT lucide-pen-line/pencil-line: those are the
  // same pencil the Edit/Read toggle uses in this very toolbar. A handwritten
  // stroke also says "freehand ink" rather than "edit this file".
  addTool(p_toolBar, PdfToolOptions::inkTool(), QStringLiteral("draw_editor.svg"), tr("Draw"),
          p_icons);
  // Text box is lucide-type. NOT type_code_editor.svg, whose `</>` means INLINE
  // CODE in the markdown toolbar and says nothing about placing a text box; a
  // bare T is what Acrobat and pdf.js both use for a free-text annotation.
  addTool(p_toolBar, PdfToolOptions::freeTextTool(), QStringLiteral("textbox_editor.svg"),
          tr("Text box"), p_icons);
}

void PdfAnnotationToolBar::addTool(QToolBar *p_toolBar, const QString &p_tool,
                                   const QString &p_iconName, const QString &p_text,
                                   const IconProvider &p_icons) {
  ToolEntry entry;

  const QIcon icon = p_icons ? p_icons(p_iconName) : QIcon();
  entry.m_action = p_toolBar->addAction(icon, p_text);
  entry.m_action->setCheckable(true);
  entry.m_action->setData(p_tool);
  m_toolGroup->addAction(entry.m_action);

  connect(entry.m_action, &QAction::triggered, this,
          [this, p_tool](bool p_checked) { emit toolToggled(p_tool, p_checked); });

  // MenuButtonPopup is what lets one button do two jobs without ambiguity: the
  // body arms the tool, the indicator opens the settings.
  entry.m_button = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(entry.m_action));
  if (entry.m_button) {
    entry.m_button->setPopupMode(QToolButton::MenuButtonPopup);
  }

  buildMenu(entry, p_tool);
  if (entry.m_button) {
    entry.m_button->setMenu(entry.m_menu);
  }

  m_tools.insert(p_tool, entry);
  m_toolOrder.append(p_tool);
}

void PdfAnnotationToolBar::buildMenu(ToolEntry &p_entry, const QString &p_tool) {
  p_entry.m_menu = new QMenu(p_entry.m_button);

  // Suppress the themed checked-icon ring on THIS menu only.
  //
  // Every interface.qss marks a checked icon-bearing action with
  // `QMenu::icon:checked { border: 2px solid @widgets#qmenu#fg; }`. Qt draws
  // that around the icon SUB-CONTROL rect, and at fractional device pixel
  // ratios it clips into a partial box — at 1.5 only the top and bottom edges
  // survive, which reads as a rendering fault. The colour rows carry a tick
  // painted into the swatch instead (CommentColorSwatch), so the ring is
  // redundant here as well as broken.
  //
  // Colourless by construction, so this is not a themed value being hardcoded
  // (see src/widgets/AGENTS.md § No Hardcoded Colors in C++) and it leaves every
  // other QMenu rule inherited from the application stylesheet intact.
  p_entry.m_menu->setStyleSheet(QStringLiteral("QMenu::icon:checked { border: none; }"));

  // Driven by the schema, NOT a hand-written list: a token added to
  // CommentColor::all() shows up in every picker at once.
  p_entry.m_colorGroup = new QActionGroup(p_entry.m_menu);
  p_entry.m_colorGroup->setExclusive(true);
  for (const auto &token : CommentColor::all()) {
    auto *act = p_entry.m_menu->addAction(swatchIcon(token), CommentColor::displayName(token));
    act->setCheckable(true);
    act->setData(token);
    p_entry.m_colorGroup->addAction(act);
    p_entry.m_colorActions.append(act);
    connect(act, &QAction::triggered, this,
            [this, p_tool, token]() { emit colorPicked(p_tool, token); });
  }

  if (PdfToolOptions::hasWidth(p_tool)) {
    p_entry.m_menu->addSeparator();
    addSliderRow(p_entry, tr("Thickness"), c_inkWidthSliderMin, c_inkWidthSliderMax,
                 p_entry.m_scalarAction, p_entry.m_scalarRow, p_entry.m_scalarSlider,
                 p_entry.m_scalarValue);
    p_entry.m_scalarSlider->setValue(sliderFromScalar(p_tool, PdfToolOptions::defaultWidth()));
  } else if (PdfToolOptions::hasFontSize(p_tool)) {
    p_entry.m_menu->addSeparator();
    addSliderRow(p_entry, tr("Font size"), c_fontSizeSliderMin, c_fontSizeSliderMax,
                 p_entry.m_scalarAction, p_entry.m_scalarRow, p_entry.m_scalarSlider,
                 p_entry.m_scalarValue);
    p_entry.m_scalarSlider->setValue(sliderFromScalar(p_tool, PdfToolOptions::defaultFontSize()));
  }

  if (p_entry.m_scalarSlider) {
    // Committed on valueChanged, NOT sliderReleased: the draft stroke has to
    // preview live, and keyboard/arrow adjustment emits nothing else.
    // ConfigMgr2 debounces the write by 500 ms, so this is not write-amplifying.
    connect(p_entry.m_scalarSlider, &QSlider::valueChanged, this, [this, p_tool](int p_value) {
      emit scalarPicked(p_tool, scalarFromSlider(p_tool, p_value));
    });
  }

  if (PdfToolOptions::hasOpacity(p_tool)) {
    addSliderRow(p_entry, tr("Opacity"), c_opacitySliderMin, c_opacitySliderMax,
                 p_entry.m_opacityAction, p_entry.m_opacityRow, p_entry.m_opacitySlider,
                 p_entry.m_opacityValue);
    p_entry.m_opacitySlider->setValue(sliderFromOpacity(PdfToolOptions::defaultOpacity()));
    connect(p_entry.m_opacitySlider, &QSlider::valueChanged, this, [this, p_tool](int p_value) {
      emit opacityPicked(p_tool, opacityFromSlider(p_value));
    });
  }

  // The labels have to show something before the first syncState(), or a menu
  // opened on a fresh window reads as broken.
  updateSliderLabels(p_entry, p_tool);
}

void PdfAnnotationToolBar::addSliderRow(ToolEntry &p_entry, const QString &p_caption, int p_min,
                                        int p_max, QWidgetAction *&p_action, QWidget *&p_row,
                                        QSlider *&p_slider, QLabel *&p_value) {
  // The QWidgetAction is parented to the menu and takes ownership of the row
  // widget; everything below is a non-owning pointer.
  p_action = new QWidgetAction(p_entry.m_menu);

  auto *row = new QWidget();
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(12, 4, 12, 4);
  layout->setSpacing(8);

  auto *caption = new QLabel(p_caption, row);
  layout->addWidget(caption);

  auto *slider = new QSlider(Qt::Horizontal, row);
  slider->setRange(p_min, p_max);
  slider->setMinimumWidth(120);
  layout->addWidget(slider, 1);

  auto *value = new QLabel(row);
  // Secondary text, via the themed property. Never setEnabled(false), and never
  // a colour literal -- see src/widgets/AGENTS.md § No Hardcoded Colors in C++.
  // Set plainly rather than through WidgetUtils::setPropertyDynamically: the row
  // has not been polished yet, so there is nothing to repolish, and this keeps
  // the component free of a gui/ dependency.
  value->setProperty(PropertyDefs::c_mutedText, true);
  value->setMinimumWidth(36);
  value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(value);

  p_action->setDefaultWidget(row);
  p_entry.m_menu->addAction(p_action);

  p_row = row;
  p_slider = slider;
  p_value = value;
}

void PdfAnnotationToolBar::updateSliderLabels(ToolEntry &p_entry, const QString &p_tool) {
  if (p_entry.m_scalarValue && p_entry.m_scalarSlider) {
    const double scalar = scalarFromSlider(p_tool, p_entry.m_scalarSlider->value());
    p_entry.m_scalarValue->setText(PdfToolOptions::hasFontSize(p_tool)
                                       ? QString::number(static_cast<int>(scalar))
                                       : QString::number(scalar, 'f', 1));
  }
  if (p_entry.m_opacityValue && p_entry.m_opacitySlider) {
    p_entry.m_opacityValue->setText(QStringLiteral("%1%").arg(p_entry.m_opacitySlider->value()));
  }
}

double PdfAnnotationToolBar::scalarFromSlider(const QString &p_tool, int p_sliderValue) {
  if (PdfToolOptions::hasWidth(p_tool)) {
    return p_sliderValue / c_inkWidthSliderScale;
  }
  if (PdfToolOptions::hasFontSize(p_tool)) {
    return static_cast<double>(p_sliderValue);
  }
  return 0.0;
}

int PdfAnnotationToolBar::sliderFromScalar(const QString &p_tool, double p_value) {
  if (PdfToolOptions::hasWidth(p_tool)) {
    return qBound(c_inkWidthSliderMin, static_cast<int>(qRound(p_value * c_inkWidthSliderScale)),
                  c_inkWidthSliderMax);
  }
  if (PdfToolOptions::hasFontSize(p_tool)) {
    return qBound(c_fontSizeSliderMin, static_cast<int>(qRound(p_value)), c_fontSizeSliderMax);
  }
  return 0;
}

double PdfAnnotationToolBar::opacityFromSlider(int p_sliderValue) {
  return p_sliderValue / c_opacitySliderScale;
}

int PdfAnnotationToolBar::sliderFromOpacity(double p_value) {
  return qBound(c_opacitySliderMin, static_cast<int>(qRound(p_value * c_opacitySliderScale)),
                c_opacitySliderMax);
}

QIcon PdfAnnotationToolBar::swatchIcon(const QString &p_token) const {
  return CommentColorSwatch::icon(m_resolve, p_token, c_swatchSizePx, m_borderCss);
}

void PdfAnnotationToolBar::setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve,
                                             QString p_borderCss) {
  // BOTH are re-supplied, not just the callback: the border travels as a value
  // and would otherwise outlive the theme it came from.
  m_resolve = std::move(p_resolve);
  m_borderCss = std::move(p_borderCss);
  rebuildSwatchIcons();
}

void PdfAnnotationToolBar::rebuildSwatchIcons() {
  for (auto it = m_tools.begin(); it != m_tools.end(); ++it) {
    for (auto *act : const_cast<const QList<QAction *> &>(it->m_colorActions)) {
      act->setIcon(swatchIcon(act->data().toString()));
    }
  }
}

void PdfAnnotationToolBar::setAuthoringEnabled(bool p_enabled) {
  for (auto it = m_tools.begin(); it != m_tools.end(); ++it) {
    if (it->m_action) {
      it->m_action->setEnabled(p_enabled);
    }
    if (it->m_button) {
      it->m_button->setEnabled(p_enabled);
    }
    if (it->m_menu) {
      it->m_menu->setEnabled(p_enabled);
    }
    // Each menu entry individually too: QAction::trigger() consults only the
    // action's OWN enabled state, so disabling the menu alone would leave every
    // row invokable.
    for (auto *act : const_cast<const QList<QAction *> &>(it->m_colorActions)) {
      act->setEnabled(p_enabled);
    }
    // The QWidgetAction AND its widget: an enabled child widget inside a
    // disabled action is still interactive in some styles.
    if (it->m_scalarAction) {
      it->m_scalarAction->setEnabled(p_enabled);
    }
    if (it->m_scalarRow) {
      it->m_scalarRow->setEnabled(p_enabled);
    }
    if (it->m_scalarSlider) {
      it->m_scalarSlider->setEnabled(p_enabled);
    }
    if (it->m_opacityAction) {
      it->m_opacityAction->setEnabled(p_enabled);
    }
    if (it->m_opacityRow) {
      it->m_opacityRow->setEnabled(p_enabled);
    }
    if (it->m_opacitySlider) {
      it->m_opacitySlider->setEnabled(p_enabled);
    }
  }
}

void PdfAnnotationToolBar::syncState(
    const QString &p_activeTool, const QHash<QString, PdfViewerConfig::ToolOptions> &p_options) {
  for (auto it = m_tools.begin(); it != m_tools.end(); ++it) {
    const auto &tool = it.key();
    const auto options = p_options.value(tool, PdfViewerConfig::ToolOptions());

    if (it->m_action) {
      // Deliberately NOT wrapped in a QSignalBlocker. setChecked() never emits
      // `triggered` (only activate() does), so there is nothing to echo back --
      // but it DOES emit `changed`, which is exactly how QActionGroup tracks
      // its current member. Blocking it leaves the group's bookkeeping stale,
      // after which a later user pick fails to clear the previous tick.
      it->m_action->setChecked(tool == p_activeTool);
    }

    for (auto *act : const_cast<const QList<QAction *> &>(it->m_colorActions)) {
      act->setChecked(act->data().toString() == options.m_color);
    }

    const double scalar = PdfToolOptions::hasWidth(tool)      ? options.m_width
                          : PdfToolOptions::hasFontSize(tool) ? options.m_fontSize
                                                              : 0.0;
    if (it->m_scalarSlider) {
      // MUST be blocked, which is the OPPOSITE of the QAction::setChecked case
      // above: QSlider::setValue DOES emit valueChanged, and that signal is the
      // very thing wired up as a user pick, so an unblocked repaint would echo
      // straight back out and persist. Do not "harmonise" the two rules.
      //
      // The slider range is deliberately narrower than the schema range, so a
      // hand-edited config value is clamped for DISPLAY only -- nothing is
      // written back.
      const QSignalBlocker blocker(it->m_scalarSlider);
      it->m_scalarSlider->setValue(sliderFromScalar(tool, scalar));
    }
    if (it->m_opacitySlider) {
      const QSignalBlocker blocker(it->m_opacitySlider);
      it->m_opacitySlider->setValue(sliderFromOpacity(options.m_opacity));
    }
    updateSliderLabels(*it, tool);
  }
}

QAction *PdfAnnotationToolBar::toolAction(const QString &p_tool) const {
  return m_tools.value(p_tool).m_action;
}

QToolButton *PdfAnnotationToolBar::toolButton(const QString &p_tool) const {
  return m_tools.value(p_tool).m_button;
}

QMenu *PdfAnnotationToolBar::toolMenu(const QString &p_tool) const {
  return m_tools.value(p_tool).m_menu;
}

QSlider *PdfAnnotationToolBar::scalarSlider(const QString &p_tool) const {
  return m_tools.value(p_tool).m_scalarSlider;
}

QWidgetAction *PdfAnnotationToolBar::scalarAction(const QString &p_tool) const {
  return m_tools.value(p_tool).m_scalarAction;
}

QSlider *PdfAnnotationToolBar::opacitySlider(const QString &p_tool) const {
  return m_tools.value(p_tool).m_opacitySlider;
}

QWidgetAction *PdfAnnotationToolBar::opacityAction(const QString &p_tool) const {
  return m_tools.value(p_tool).m_opacityAction;
}

QList<QAction *> PdfAnnotationToolBar::toolActions() const {
  QList<QAction *> actions;
  for (const auto &tool : m_toolOrder) {
    if (auto *act = toolAction(tool)) {
      actions.append(act);
    }
  }
  return actions;
}

bool PdfAnnotationToolBar::isToolGroupExclusive() const {
  return m_toolGroup && m_toolGroup->isExclusive();
}
