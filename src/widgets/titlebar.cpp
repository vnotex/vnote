#include "titlebar.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QToolButton>
#include <QAction>

#include "vnotex.h"
#include "thememgr.h"
#include <utils/iconutils.h>
#include "widgetsfactory.h"

#include "propertydefs.h"

using namespace vnotex;

const char *TitleBar::c_titleProp = "TitleBarTitle";

const QString TitleBar::c_actionButtonForegroundName = "widgets#titlebar#button#fg";

const QString TitleBar::c_menuIconForegroundName = "widgets#titlebar#menu_icon#fg";

const QString TitleBar::c_menuIconDisabledForegroundName = "widgets#titlebar#menu_icon#disabled#fg";

TitleBar::TitleBar(const QString &p_title,
                   bool p_hasInfoLabel,
                   TitleBar::Actions p_actionFlags,
                   QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI(p_title, p_hasInfoLabel, p_actionFlags);
}

void TitleBar::setupUI(const QString &p_title, bool p_hasInfoLabel, TitleBar::Actions p_actionFlags)
{
    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Title label.
    // Should always add it even if title is empty. Otherwise, we could not catch the hover event to show actions.
    {
        auto titleLabel = new QLabel(p_title, this);
        titleLabel->setProperty(c_titleProp, true);
        mainLayout->addWidget(titleLabel);
    }

    mainLayout->addStretch();

    {
        m_buttonWidget = new QWidget(this);
        mainLayout->addWidget(m_buttonWidget);

        auto btnLayout = new QHBoxLayout(m_buttonWidget);
        btnLayout->setContentsMargins(0, 0, 0, 0);

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

QToolButton *TitleBar::newActionButton(const QString &p_iconName, const QString &p_text, QWidget *p_parent)
{
    auto btn = new QToolButton(p_parent);
    btn->setProperty(PropertyDefs::c_actionToolButton, true);

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    auto iconFile = themeMgr.getIconFile(p_iconName);
    const auto fg = themeMgr.paletteColor(c_actionButtonForegroundName);
    auto icon = IconUtils::fetchIcon(iconFile, fg);

    auto act = new QAction(icon, p_text, btn);
    btn->setDefaultAction(act);
    return btn;
}

void TitleBar::setupActionButtons(TitleBar::Actions p_actionFlags)
{
    if (p_actionFlags & Action::Menu) {
        m_menu = WidgetsFactory::createMenu(this);
        addActionButton("menu.svg", tr("Menu"), m_menu);
    }

    if (p_actionFlags & Action::Settings) {
        auto btn = addActionButton("settings.svg", tr("Settings"));
        connect(btn, &QToolButton::triggered,
                this, []() {
                    // TODO.
                });
    }
}

void TitleBar::enterEvent(QEnterEvent *p_event)
{
    QFrame::enterEvent(p_event);
    setActionButtonsVisible(true);
}

void TitleBar::leaveEvent(QEvent *p_event)
{
    QWidget::leaveEvent(p_event);
    setActionButtonsVisible(m_actionButtonsForcedShown || m_actionButtonsAlwaysShown);
}

void TitleBar::setActionButtonsVisible(bool p_visible)
{
    m_buttonWidget->setVisible(p_visible);
}

QIcon TitleBar::generateMenuActionIcon(const QString &p_iconName)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(c_menuIconForegroundName);
    const auto disabledFg = themeMgr.paletteColor(c_menuIconDisabledForegroundName);

    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));

    auto iconFile = themeMgr.getIconFile(p_iconName);
    return IconUtils::fetchIcon(iconFile, colors);
}

QAction *TitleBar::addMenuAction(const QString &p_iconName, const QString &p_text)
{
    auto act = m_menu->addAction(generateMenuActionIcon(p_iconName), p_text);
    return act;
}

QMenu *TitleBar::addMenuSubMenu(const QString &p_text)
{
    return m_menu->addMenu(p_text);
}

void TitleBar::addMenuSeparator()
{
    Q_ASSERT(m_menu);
    m_menu->addSeparator();
}

QToolButton *TitleBar::addActionButton(const QString &p_iconName, const QString &p_text)
{
    auto btn = newActionButton(p_iconName, p_text, this);
    auto layout = actionButtonLayout();
    if (!m_menu) {
        m_actionButtons.push_back(btn);
        layout->addWidget(btn);
    } else {
        int idx = m_actionButtons.size() - 1;
        if (idx < 0) {
            idx = 0;
        }
        m_actionButtons.insert(idx, btn);
        layout->insertWidget(idx, btn);
    }
    return btn;
}

QToolButton *TitleBar::addActionButton(const QString &p_iconName, const QString &p_text, QMenu *p_menu)
{
    p_menu->setParent(this);

    auto btn = addActionButton(p_iconName, p_text);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setMenu(p_menu);
    connect(p_menu, &QMenu::aboutToShow,
            this, [this]() {
                m_actionButtonsForcedShown = true;
                setActionButtonsVisible(true);
            });
    connect(p_menu, &QMenu::aboutToHide,
            this, [this]() {
                m_actionButtonsForcedShown = false;
                setActionButtonsVisible(m_actionButtonsAlwaysShown);
            });
    return btn;
}

QHBoxLayout *TitleBar::actionButtonLayout() const
{
    return static_cast<QHBoxLayout *>(m_buttonWidget->layout());
}

void TitleBar::setInfoLabel(const QString &p_info)
{
    Q_ASSERT(m_infoLabel);
    if (m_infoLabel) {
        m_infoLabel->setText(p_info);
    }
}

void TitleBar::setActionButtonsAlwaysShown(bool p_shown)
{
    m_actionButtonsAlwaysShown = p_shown;
    setActionButtonsVisible(m_actionButtonsForcedShown || m_actionButtonsAlwaysShown);
}
