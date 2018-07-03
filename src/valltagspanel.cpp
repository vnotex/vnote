#include "valltagspanel.h"

#include <QtWidgets>

#include "vtaglabel.h"
#include "utils/vimnavigationforwidget.h"

VAllTagsPanel::VAllTagsPanel(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_list = new QListWidget(this);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_list);

    setLayout(layout);
}

void VAllTagsPanel::clear()
{
    while (m_list->count() > 0) {
        removeItem(m_list->item(0));
    }
}

void VAllTagsPanel::removeItem(QListWidgetItem *p_item)
{
    QWidget *wid = m_list->itemWidget(p_item);
    m_list->removeItemWidget(p_item);
    wid->deleteLater();

    int row = m_list->row(p_item);
    Q_ASSERT(row >= 0);
    m_list->takeItem(row);
    delete p_item;
}

VTagLabel *VAllTagsPanel::addTag(const QString &p_text)
{
    VTagLabel *label = new VTagLabel(p_text, true, this);
    QSize sz = label->sizeHint();
    sz.setHeight(sz.height() * 2 + 10);

    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(sz);

    connect(label, &VTagLabel::removalRequested,
            this, [this, item](const QString &p_text) {
                removeItem(item);
                emit tagRemoved(p_text);
            });

    m_list->addItem(item);
    m_list->setItemWidget(item, label);
    return label;
}

void VAllTagsPanel::showEvent(QShowEvent *p_event)
{
    QWidget::showEvent(p_event);

    m_list->setFocus();
}

void VAllTagsPanel::keyPressEvent(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(m_list, p_event)) {
        return;
    }

    QWidget::keyPressEvent(p_event);
}
