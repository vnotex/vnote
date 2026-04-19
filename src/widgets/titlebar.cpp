#include "titlebar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QToolButton>

#include <gui/services/themeservice.h>

#include "lineedit.h"
#include "widgetsfactory.h"
#include <gui/utils/iconutils.h>

#include "propertydefs.h"

using namespace vnotex;

const char *TitleBar::c_titleProp = "TitleBarTitle";

const QString TitleBar::c_actionButtonForegroundName = "widgets#titlebar#button#fg";

const QString TitleBar::c_menuIconForegroundName = "widgets#titlebar#menu_icon#fg";

const QString TitleBar::c_menuIconDisabledForegroundName = "widgets#titlebar#menu_icon#disabled#fg";

TitleBar::TitleBar(ThemeService *p_themeService, const QString &p_title, bool p_hasInfoLabel,
                   TitleBar::Actions p_actionFlags, QWidget *p_parent)
    : QFrame(p_parent), m_themeService(p_themeService) {
  setupUI(p_title, p_hasInfoLabel, p_actionFlags);
  connect(p_themeService, &ThemeService::themeChanged, this, &TitleBar::refreshIcons);
}

void TitleBar::setupUI(const QString &p_title, bool p_hasInfoLabel,
                       TitleBar::Actions p_actionFlags) {
  auto mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title label.
  // Should always add it even if title is empty. Otherwise, we could not catch the hover event to
  // show actions.
  {
    auto titleLabel = new QLabel(p_title, this);
    titleLabel->setProperty(c_titleProp, true);
    mainLayout->addWidget(titleLabel);
  }

  m_spacer = new QWidget(this);
  m_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  mainLayout->addWidget(m_spacer);

  {
    m_buttonToolBar = new QToolBar(this);
    m_buttonToolBar->setMovable(false);
    m_buttonToolBar->setFloatable(false);
    m_buttonToolBar->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_buttonToolBar);

    setupActionButtons(p_actionFlags);

    setActionButtonsVisible(false);
  }

  // Info label.
  if (p_hasInfoLabel) {
    m_infoLabel = new QLabel(this);
    m_infoLabel->setProperty(c_titleProp, true);
    mainLayout->addWidget(m_infoLabel);
  }

  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QToolButton *TitleBar::newActionButton(const QString &p_iconName, const QString &p_text,
                                       QWidget *p_parent) {
  auto btn = new QToolButton(p_parent);
  btn->setProperty(PropertyDefs::c_actionToolButton, true);

  auto iconFile = m_themeService->getIconFile(p_iconName);
  const auto fg = m_themeService->paletteColor(c_actionButtonForegroundName);
  auto icon = IconUtils::fetchIcon(iconFile, fg);

  auto act = new QAction(icon, p_text, btn);
  btn->setDefaultAction(act);
  m_trackedIcons.push_back({act, p_iconName, false});
  return btn;
}

void TitleBar::setupActionButtons(TitleBar::Actions p_actionFlags) {
  if (p_actionFlags & Action::Menu) {
    m_menu = WidgetsFactory::createMenu(this);
    addActionButton("menu.svg", tr("Menu"), m_menu);
  }

  if (p_actionFlags & Action::Settings) {
    auto btn = addActionButton("settings.svg", tr("Settings"));
    connect(btn, &QToolButton::triggered, this, []() {
      // TODO.
    });
  }

  if (p_actionFlags & Action::Search) {
    // Create search edit (hidden initially).
    m_searchEdit = new LineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchEdit->setVisible(false);

    auto *mainLayout = qobject_cast<QHBoxLayout *>(layout());
    Q_ASSERT(mainLayout);
    // Insert after title label (index 0), before spacer (index 1).
    mainLayout->insertWidget(1, m_searchEdit);

    connect(m_searchEdit, &LineEdit::textChanged, this, &TitleBar::searchTextChanged);

    // Create toggle button. Checkable must be set on the QAction (not the
    // QToolButton) because setDefaultAction() synchronises button properties
    // from the action, overriding any direct button state.
    m_searchButton = addActionButton(QStringLiteral("search.svg"), tr("Search"));
    m_searchButton->defaultAction()->setCheckable(true);
    connect(m_searchButton->defaultAction(), &QAction::toggled, this, &TitleBar::setSearchBoxVisible);
  }
}

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
void TitleBar::enterEvent(QEvent *p_event)
#else
void TitleBar::enterEvent(QEnterEvent *p_event)
#endif
{
  QFrame::enterEvent(p_event);
  setActionButtonsVisible(true);
}

void TitleBar::leaveEvent(QEvent *p_event) {
  QWidget::leaveEvent(p_event);
  setActionButtonsVisible(m_actionButtonsForcedShown || m_actionButtonsAlwaysShown);
}

void TitleBar::setActionButtonsVisible(bool p_visible) { m_buttonToolBar->setVisible(p_visible); }

QIcon TitleBar::generateMenuActionIcon(const QString &p_iconName) {
  const auto fg = m_themeService->paletteColor(c_menuIconForegroundName);
  const auto disabledFg = m_themeService->paletteColor(c_menuIconDisabledForegroundName);

  QVector<IconUtils::OverriddenColor> colors;
  colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
  colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));

  auto iconFile = m_themeService->getIconFile(p_iconName);
  return IconUtils::fetchIcon(iconFile, colors);
}

QAction *TitleBar::addMenuAction(const QString &p_iconName, const QString &p_text) {
  auto act = m_menu->addAction(generateMenuActionIcon(p_iconName), p_text);
  m_trackedIcons.push_back({act, p_iconName, true});
  return act;
}

void TitleBar::addMenuAction(QAction *p_action) {
  m_menu->addAction(p_action);
}

QMenu *TitleBar::addMenuSubMenu(const QString &p_text) { return m_menu->addMenu(p_text); }

void TitleBar::addMenuSeparator() {
  Q_ASSERT(m_menu);
  m_menu->addSeparator();
}

QToolButton *TitleBar::addActionButton(const QString &p_iconName, const QString &p_text) {
  auto btn = newActionButton(p_iconName, p_text, m_buttonToolBar);
  if (!m_menu) {
    m_actionButtons.push_back(btn);
    m_buttonToolBar->addWidget(btn);
  } else {
    // Insert before the menu button (last button in m_actionButtons)
    int idx = m_actionButtons.size() - 1;
    if (idx < 0) {
      idx = 0;
    }
    m_actionButtons.insert(idx, btn);

    // Find the menu button's action to insert before it
    auto menuBtn = m_actionButtons.last();
    QAction *beforeAction = nullptr;
    for (auto *action : m_buttonToolBar->actions()) {
      if (m_buttonToolBar->widgetForAction(action) == menuBtn) {
        beforeAction = action;
        break;
      }
    }
    m_buttonToolBar->insertWidget(beforeAction, btn);
  }
  return btn;
}

QToolButton *TitleBar::addActionButton(const QString &p_iconName, const QString &p_text,
                                       QMenu *p_menu) {
  p_menu->setParent(this);

  auto btn = addActionButton(p_iconName, p_text);
  btn->setPopupMode(QToolButton::InstantPopup);
  btn->setMenu(p_menu);
  connect(p_menu, &QMenu::aboutToShow, this, [this]() {
    m_actionButtonsForcedShown = true;
    setActionButtonsVisible(true);
  });
  connect(p_menu, &QMenu::aboutToHide, this, [this]() {
    m_actionButtonsForcedShown = false;
    setActionButtonsVisible(m_actionButtonsAlwaysShown);
  });
  return btn;
}

void TitleBar::setInfoLabel(const QString &p_info) {
  Q_ASSERT(m_infoLabel);
  if (m_infoLabel) {
    m_infoLabel->setText(p_info);
  }
}

void TitleBar::setActionButtonsAlwaysShown(bool p_shown) {
  m_actionButtonsAlwaysShown = p_shown;
  setActionButtonsVisible(m_actionButtonsForcedShown || m_actionButtonsAlwaysShown);
}

void TitleBar::setSearchPlaceholder(const QString &p_placeholderText) {
  if (m_searchEdit) {
    m_searchEdit->setPlaceholderText(p_placeholderText);
  }
}

void TitleBar::setSearchBoxVisible(bool p_visible) {
  if (!m_searchEdit) {
    return;
  }
  m_searchEdit->setVisible(p_visible);
  m_spacer->setVisible(!p_visible);
  if (p_visible) {
    m_searchEdit->setFocus();
  } else {
    m_searchEdit->clear();
  }
}

void TitleBar::refreshIcons() {
  for (auto &tracked : m_trackedIcons) {
    if (tracked.m_isMenuIcon) {
      tracked.m_action->setIcon(generateMenuActionIcon(tracked.m_iconName));
    } else {
      auto iconFile = m_themeService->getIconFile(tracked.m_iconName);
      const auto fg = m_themeService->paletteColor(c_actionButtonForegroundName);
      tracked.m_action->setIcon(IconUtils::fetchIcon(iconFile, fg));
    }
  }
}
