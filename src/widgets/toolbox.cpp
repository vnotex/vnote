#include "toolbox.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QVariant>
#include <QToolButton>
#include <QAction>
#include <QLabel>

#include <utils/utils.h>
#include "global.h"
#include <utils/iconutils.h>
#include "thememgr.h"
#include "vnotex.h"

using namespace vnotex;

const char *ToolBox::c_titleProp = "ToolBoxTitle";

const char *ToolBox::c_titleButtonProp = "ToolBoxTitleButton";

const QString ToolBox::c_titleButtonForegroundName = "widgets#toolbox#title#button#fg";

const QString ToolBox::c_titleButtonActiveForegroundName = "widgets#toolbox#title#button#active#fg";

ToolBox::ToolBox(QWidget *p_parent)
    : QFrame(p_parent),
      NavigationMode(NavigationMode::Type::DoubleKeys, this),
      m_buttonLayout(nullptr),
      m_widgetLayout(nullptr),
      m_buttonActionGroup(nullptr),
      m_currentIndex(-1)
{
    setupUI();
}

void ToolBox::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(CONTENTS_MARGIN, 0, CONTENTS_MARGIN, 0);

    auto titleWid = new QWidget(this);
    titleWid->setProperty(c_titleProp, true);
    mainLayout->addWidget(titleWid);

    m_buttonLayout = new QHBoxLayout(titleWid);
    m_buttonLayout->addStretch();
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(0);

    m_buttonActionGroup = new QActionGroup(titleWid);

    m_widgetLayout = new QStackedLayout();
    mainLayout->addLayout(m_widgetLayout);
}

int ToolBox::addItem(QWidget *p_widget,
                     const QString &p_iconFile,
                     const QString &p_text,
                     QWidget *p_focusWidget)
{
    int idx = m_items.size();

    auto icon = generateTitleIcon(p_iconFile);
    auto btn = generateItemButton(icon, p_text, idx);
    m_buttonLayout->insertWidget(idx, btn);
    m_widgetLayout->insertWidget(idx, p_widget);

    ItemInfo item;
    item.m_widget = p_widget;
    item.m_focusWidget = p_focusWidget;
    item.m_text = p_text;
    item.m_button = btn;

    m_items.push_back(item);

    if (idx == 0) {
        setCurrentIndex(idx, false);
    }

    return idx;
}

QVector<void *> ToolBox::getVisibleNavigationItems()
{
    QVector<void *> items;
    items.reserve(m_items.size());
    for (const auto &item : m_items) {
        items.push_back(item.m_widget);
    }
    return items;
}

void ToolBox::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label)
{
    Q_UNUSED(p_item);
    QRect rect = m_items[p_idx].m_button->geometry();
    p_label->move(rect.x(), rect.y() + rect.height() / 2);
}

void ToolBox::handleTargetHit(void *p_item)
{
    if (p_item) {
        auto widget = static_cast<QWidget *>(p_item);
        setCurrentWidget(widget, true);
    }
}

void ToolBox::setCurrentWidget(QWidget *p_widget, bool p_focus)
{
    int idx = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].m_widget == p_widget) {
            idx = i;
            break;
        }
    }

    setCurrentIndex(idx, p_focus);
}

QIcon ToolBox::generateTitleIcon(const QString &p_iconFile) const
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(c_titleButtonForegroundName);
    const auto activeFg = themeMgr.paletteColor(c_titleButtonActiveForegroundName);

    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal, QIcon::Off));
    colors.push_back(IconUtils::OverriddenColor(activeFg, QIcon::Normal, QIcon::On));

    return IconUtils::fetchIcon(p_iconFile, colors);
}

QToolButton *ToolBox::generateItemButton(const QIcon &p_icon, const QString &p_text, int p_itemIdx) const
{
    auto btn = new QToolButton();
    btn->setProperty(c_titleButtonProp, true);

    auto act = new QAction(p_icon, p_text, m_buttonActionGroup);
    act->setCheckable(true);
    act->setData(p_itemIdx);

    btn->setDefaultAction(act);
    connect(btn, &QToolButton::triggered,
            this, [this](QAction *p_action) {
                const_cast<ToolBox *>(this)->setCurrentIndex(p_action->data().toInt(), true);
            });

    return btn;
}

void ToolBox::setCurrentIndex(int p_idx, bool p_focus)
{
    if (p_idx < 0 || p_idx >= m_items.size()) {
        m_currentIndex = m_items.isEmpty() ? -1 : 0;
    } else {
        m_currentIndex = p_idx;
    }

    setCurrentButtonIndex(m_currentIndex);
    m_widgetLayout->setCurrentIndex(m_currentIndex);

    auto widget = m_widgetLayout->widget(m_currentIndex);
    if (widget && p_focus) {
        if (m_items[m_currentIndex].m_focusWidget) {
            m_items[m_currentIndex].m_focusWidget->setFocus();
        } else {
            widget->setFocus();
        }
    }
}

void ToolBox::setCurrentButtonIndex(int p_idx)
{
    for (int i = 0; i < m_items.size(); ++i) {
        auto btn = m_items[i].m_button;
        btn->setChecked(p_idx == i);
        btn->setToolButtonStyle(p_idx == i ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    }
}

void ToolBox::focusInEvent(QFocusEvent *p_event)
{
    QFrame::focusInEvent(p_event);

    // Focus current tab.
    setCurrentIndex(m_currentIndex, true);
}
