#include "vtoolbox.h"

#include <QPushButton>
#include <QStackedLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QVariant>


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
    if (m_currentIndex == p_idx) {
        return;
    }

    if (p_idx < 0 || p_idx >= m_items.size()) {
        m_currentIndex = -1;
    } else {
        m_currentIndex = p_idx;
    }

    setCurrentButtonIndex(m_currentIndex);

    m_widgetLayout->setCurrentIndex(m_currentIndex);
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
