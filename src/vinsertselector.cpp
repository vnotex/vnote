#include "vinsertselector.h"

#include <QtWidgets>
#include "utils/vutils.h"

VSelectorItemWidget::VSelectorItemWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
}

VSelectorItemWidget::VSelectorItemWidget(const VInsertSelectorItem &p_item, QWidget *p_parent)
    : QWidget(p_parent), m_name(p_item.m_name)
{
    QLabel *shortcutLabel = new QLabel(p_item.m_shortcut);
    shortcutLabel->setProperty("SelectorItemShortcutLabel", true);

    m_btn = new QPushButton(p_item.m_name);
    m_btn->setToolTip(p_item.m_toolTip);
    m_btn->setProperty("SelectionBtn", true);
    connect(m_btn, &QPushButton::clicked,
            this, [this]() {
                emit clicked(m_name);
            });

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(shortcutLabel);
    layout->addWidget(m_btn, 1);
    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
}

VInsertSelector::VInsertSelector(int p_nrRows,
                                 const QVector<VInsertSelectorItem> &p_items,
                                 QWidget *p_parent)
    : QWidget(p_parent),
      m_items(p_items)
{
    setupUI(p_nrRows < 1 ? 1 : p_nrRows);
}

void VInsertSelector::setupUI(int p_nrRows)
{
    QGridLayout *layout = new QGridLayout();

    int row = 0, col = 0;
    for (auto const & it : m_items) {
        QWidget *wid = createItemWidget(it);
        layout->addWidget(wid, row, col);
        if (++row == p_nrRows) {
            row = 0;
            ++col;
        }
    }

    setLayout(layout);
}

QWidget *VInsertSelector::createItemWidget(const VInsertSelectorItem &p_item)
{
    VSelectorItemWidget *widget = new VSelectorItemWidget(p_item);
    connect(widget, &VSelectorItemWidget::clicked,
            this, &VInsertSelector::itemClicked);

    return widget;
}

void VInsertSelector::itemClicked(const QString &p_name)
{
    m_clickedItemName = p_name;
    emit accepted(true);
}

void VInsertSelector::keyPressEvent(QKeyEvent *p_event)
{
    QWidget::keyPressEvent(p_event);

    if (p_event->key() == Qt::Key_BracketLeft
        && VUtils::isControlModifierForVim(p_event->modifiers())) {
        m_clickedItemName.clear();
        emit accepted(false);
        return;
    }

    QChar ch = VUtils::keyToChar(p_event->key());
    if (!ch.isNull()) {
        // Activate corresponding item.
        const VInsertSelectorItem *item = findItemByShortcut(ch);
        if (item) {
            itemClicked(item->m_name);
        }
    }
}

const VInsertSelectorItem *VInsertSelector::findItemByShortcut(QChar p_shortcut) const
{
    for (auto const & it : m_items) {
        if (it.m_shortcut == p_shortcut) {
            return &it;
        }
    }

    return NULL;
}

void VInsertSelector::showEvent(QShowEvent *p_event)
{
    QWidget::showEvent(p_event);

    if (!hasFocus()) {
        setFocus();
    }
}

void VInsertSelector::paintEvent(QPaintEvent *p_event)
{
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    QWidget::paintEvent(p_event);
}
