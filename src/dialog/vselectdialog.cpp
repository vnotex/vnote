#include <QtWidgets>

#include "vselectdialog.h"
#include "utils/vimnavigationforwidget.h"

#define CANCEL_ID -1

VSelectDialog::VSelectDialog(const QString &p_title, QWidget *p_parent)
    : QDialog(p_parent), m_choice(-1)
{
    setupUI(p_title);
}

void VSelectDialog::setupUI(const QString &p_title)
{
    m_list = new QListWidget();
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_list, &QListWidget::itemActivated,
            this, &VSelectDialog::selectionChosen);

    // Add cancel item.
    QListWidgetItem *cancelItem = new QListWidgetItem(tr("Cancel"));
    cancelItem->setData(Qt::UserRole, CANCEL_ID);

    m_list->addItem(cancelItem);
    m_list->setCurrentRow(0);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_list);

    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
    setWindowTitle(p_title);
}

void VSelectDialog::addSelection(const QString &p_selectStr, int p_selectID)
{
    Q_ASSERT(p_selectID >= 0);

    QListWidgetItem *item = new QListWidgetItem(p_selectStr);
    item->setData(Qt::UserRole, p_selectID);
    m_list->insertItem(m_list->count() - 1, item);

    m_list->setCurrentRow(0);
}

void VSelectDialog::selectionChosen(QListWidgetItem *p_item)
{
    m_choice = p_item->data(Qt::UserRole).toInt();
    if (m_choice == CANCEL_ID) {
        reject();
    } else {
        accept();
    }
}

int VSelectDialog::getSelection() const
{
    return m_choice;
}

void VSelectDialog::updateSize()
{
    Q_ASSERT(m_list->count() > 0);

    int height = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        height += m_list->sizeHintForRow(i);
    }

    height += 2 * m_list->count();
    int wid = width();
    m_list->resize(wid, height);
    resize(wid, height);
}

void VSelectDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    updateSize();
}

void VSelectDialog::keyPressEvent(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(m_list,
                                                          p_event,
                                                          this)) {
        return;
    }

    QDialog::keyPressEvent(p_event);
}

