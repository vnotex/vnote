#include "vtoolbox.h"

#include <QPushButton>
#include <QStackedLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QVariant>
#include <QLabel>

#include "vnote.h"
#include "utils/vutils.h"

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

    m_widgetLayout = new QStackedLayout();

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(m_btnLayout);
    mainLayout->addLayout(m_widgetLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

int VToolBox::addItem(QWidget *p_widget, const QIcon &p_iconSet, const QString &p_text)
{
    int idx = m_items.size();

    // New a button.
    QPushButton *btn = new QPushButton(p_iconSet, "");
    btn->setToolTip(p_text);
    btn->setProperty("FlatBtn", true);
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

    m_items.push_back(ItemInfo(p_widget, p_text, btn));

    if (m_items.size() == 1) {
        setCurrentIndex(0);
    }

    return idx;
}

void VToolBox::setCurrentIndex(int p_idx)
{
    if (p_idx < 0 || p_idx >= m_items.size()) {
        m_currentIndex = -1;
    } else {
        m_currentIndex = p_idx;
    }

    setCurrentButtonIndex(m_currentIndex);

    m_widgetLayout->setCurrentIndex(m_currentIndex);

    QWidget *widget = m_widgetLayout->widget(m_currentIndex);
    if (widget) {
        widget->setFocus();
    }
}

void VToolBox::setCurrentWidget(QWidget *p_widget)
{
    int idx = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].m_widget == p_widget) {
            idx = i;
            break;
        }
    }

    setCurrentIndex(idx);
}

void VToolBox::setCurrentButtonIndex(int p_idx)
{
    // Remove the text of all button.
    for (int i = 0; i < m_items.size(); ++i) {
        QPushButton *btn = m_items[i].m_btn;
        btn->setText("");
        btn->clearFocus();
    }

    if (p_idx < 0 || p_idx >= m_items.size()) {
        return;
    }

    m_items[p_idx].m_btn->setText(m_items[p_idx].m_text);
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
        ret = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
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
