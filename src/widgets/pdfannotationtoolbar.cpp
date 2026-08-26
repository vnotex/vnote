#include "pdfannotationtoolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>

#include <core/services/commenttypes.h>

using namespace vnotex;

namespace {

// Size of the colour chip painted into every menu row.
constexpr int c_swatchSizePx = 16;

struct ScalarChoice {
  const char *m_label;
  double m_value;
};

// Ink stroke widths, in PDF units. Inside PdfInkAnchor::min/maxWidth().
const ScalarChoice c_inkWidths[] = {
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Thin"), 0.75},
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Medium"), 1.5},
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Thick"), 3.0}};

// Free-text font sizes, in PDF units. Inside PdfFreeTextAnchor::min/maxFontSize().
const ScalarChoice c_fontSizes[] = {
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Small"), 9.0},
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Medium"), 12.0},
    {QT_TRANSLATE_NOOP("vnotex::PdfAnnotationToolBar", "Large"), 16.0}};

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
  // pixels apart is worse than no icon. A handwritten squiggle also says
  // "freehand ink" rather than "edit this file".
  addTool(p_toolBar, PdfToolOptions::inkTool(), QStringLiteral("draw_editor.svg"), tr("Draw"),
          p_icons);
  // NOT type_code_editor.svg: that `</>` means INLINE CODE in the markdown
  // toolbar. A T in a box is what every PDF tool (Acrobat, pdf.js) uses for a
  // free-text annotation, and it says what the tool actually places.
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

  const ScalarChoice *choices = nullptr;
  int count = 0;
  if (PdfToolOptions::hasWidth(p_tool)) {
    choices = c_inkWidths;
    count = static_cast<int>(sizeof(c_inkWidths) / sizeof(c_inkWidths[0]));
  } else if (PdfToolOptions::hasFontSize(p_tool)) {
    choices = c_fontSizes;
    count = static_cast<int>(sizeof(c_fontSizes) / sizeof(c_fontSizes[0]));
  }

  if (count > 0) {
    p_entry.m_menu->addSeparator();
    // A SECOND, independently exclusive group: picking a width must not clear
    // the colour tick.
    p_entry.m_scalarGroup = new QActionGroup(p_entry.m_menu);
    p_entry.m_scalarGroup->setExclusive(true);
    for (int i = 0; i < count; ++i) {
      const double value = choices[i].m_value;
      auto *act = p_entry.m_menu->addAction(tr(choices[i].m_label));
      act->setCheckable(true);
      act->setData(value);
      p_entry.m_scalarGroup->addAction(act);
      p_entry.m_scalarActions.append(act);
      connect(act, &QAction::triggered, this,
              [this, p_tool, value]() { emit scalarPicked(p_tool, value); });
    }
  }
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
    for (auto *act : const_cast<const QList<QAction *> &>(it->m_scalarActions)) {
      act->setEnabled(p_enabled);
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
    for (auto *act : const_cast<const QList<QAction *> &>(it->m_scalarActions)) {
      act->setChecked(qFuzzyCompare(act->data().toDouble(), scalar));
    }
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
