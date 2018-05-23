#include "vtoolbox.h"

#include <QPushButton>
#include <QStackedLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QVariant>
#include <QLabel>

#include "vnote.h"
#include "utils/vutils.h"
#include "utils/viconutils.h"

extern VNote *g_vnote;

VToolBox::VToolBox(QWidget *p_parent)
    : QWidget(p_parent),
      m_currentIndex(-1)
{
    setupUI();
}

void VToolBox::setupUI()
{
    m_btnLayout = new QHBoxLayout();
    m_btnLayout->addStretch();
    m_btnLayout->setContentsMargins(0, 0, 0, 2);
    m_btnLayout->setSpacing(0);
    QWidget *wid = new QWidget();
    wid->setProperty("ToolBoxTitle", true);
    wid->setLayout(m_btnLayout);

    m_widgetLayout = new QStackedLayout();

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(wid);
    mainLayout->addLayout(m_widgetLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

int VToolBox::addItem(QWidget *p_widget,
                      const QString &p_iconFile,
                      const QString &p_text,
                      QWidget *p_focusWidget)
{
    int idx = m_items.size();

    // New a button.
    QIcon icon = VIconUtils::toolBoxIcon(p_iconFile);
    QPushButton *btn = new QPushButton(icon, "");
    btn->setToolTip(p_text);
    btn->setProperty("FlatBtn", true);
    btn->setProperty("ToolBoxTitleBtn", true);
    connect(btn, &QPushButton::clicked,
            this, [this]() {
                QObject *btn = sender();
                for (int i = 0; i < m_items.size(); ++i) {
                    if (m_items[i].m_btn == btn) {
                        setCurrentIndex(i);
                        break;
                    }
                }
            });

    m_btnLayout->insertWidget(idx, btn);

    // Insert widget to layout.
    m_widgetLayout->insertWidget(idx, p_widget);

    m_items.push_back(ItemInfo(p_widget,
                               p_focusWidget,
                               p_text,
                               btn,
                               icon,
                               VIconUtils::toolBoxActiveIcon(p_iconFile)));

    if (m_items.size() == 1) {
        setCurrentIndex(0);
    }

    return idx;
}

void VToolBox::setCurrentIndex(int p_idx, bool p_focus)
{
    if (p_idx < 0 || p_idx >= m_items.size()) {
        if (m_items.isEmpty()) {
            m_currentIndex = -1;
        } else {
            m_currentIndex = 0;
        }
    } else {
        m_currentIndex = p_idx;
    }

    setCurrentButtonIndex(m_currentIndex);

    m_widgetLayout->setCurrentIndex(m_currentIndex);

    QWidget *widget = m_widgetLayout->widget(m_currentIndex);
    if (widget && p_focus) {
        if (m_items[m_currentIndex].m_focusWidget) {
            m_items[m_currentIndex].m_focusWidget->setFocus();
        } else {
            widget->setFocus();
        }
    }
}

void VToolBox::setCurrentWidget(QWidget *p_widget, bool p_focus)
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

void VToolBox::setCurrentButtonIndex(int p_idx)
{
    // Remove the text of all button.
    for (int i = 0; i < m_items.size(); ++i) {
        QPushButton *btn = m_items[i].m_btn;
        btn->setText("");
        btn->setIcon(m_items[i].m_icon);
        btn->clearFocus();
        VUtils::setDynamicProperty(btn, "ToolBoxActiveBtn", false);
    }

    if (p_idx < 0 || p_idx >= m_items.size()) {
        return;
    }

    QPushButton *curBtn = m_items[p_idx].m_btn;
    curBtn->setText(m_items[p_idx].m_text);
    curBtn->setIcon(m_items[p_idx].m_activeIcon);
    VUtils::setDynamicProperty(curBtn, "ToolBoxActiveBtn", true);
}

void VToolBox::showNavigation()
{
    clearNavigation();

    if (!isVisible()) {
        return;
    }

    for (int i = 0; i < 26 && i < m_items.size(); ++i) {
        const ItemInfo &item = m_items[i];

        QChar key('a' + i);
        m_keyMap[key] = item.m_widget;

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, this);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->show();
        QRect rect = item.m_btn->geometry();
        // Display the label at the end to show the file name.
        label->move(rect.x(), rect.y() + rect.height() / 2);
        m_naviLabels.append(label);
    }
}

bool VToolBox::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (secondKey && !keyChar.isNull()) {
        secondKey = false;
        p_succeed = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            ret = true;
            QWidget *widget = static_cast<QWidget *>(it.value());
            setCurrentWidget(widget);
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            secondKey = true;
        }

        ret = true;
    }

    return ret;
}
