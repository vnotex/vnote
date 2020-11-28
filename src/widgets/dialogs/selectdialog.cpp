#include "selectdialog.h"

#include <QtWidgets>

#include <utils/widgetutils.h>

using namespace vnotex;

SelectDialog::SelectDialog(const QString &p_title, QWidget *p_parent)
    : QDialog(p_parent)
{
    setupUI(p_title);
}

void SelectDialog::setupUI(const QString &p_title)
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(this);
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_list, &QListWidget::itemActivated,
            this, &SelectDialog::selectionChosen);

    // Add cancel item.
    QListWidgetItem *cancelItem = new QListWidgetItem(tr("Cancel"));
    cancelItem->setData(Qt::UserRole, CANCEL_ID);

    m_list->addItem(cancelItem);
    m_list->setCurrentRow(0);

    mainLayout->addWidget(m_list);

    setWindowTitle(p_title);
}

void SelectDialog::addSelection(const QString &p_selectStr, int p_selectID)
{
    Q_ASSERT(p_selectID >= 0);

    QListWidgetItem *item = new QListWidgetItem(p_selectStr);
    item->setData(Qt::UserRole, p_selectID);
    m_list->insertItem(m_list->count() - 1, item);

    m_list->setCurrentRow(0);
}

void SelectDialog::selectionChosen(QListWidgetItem *p_item)
{
    m_choice = p_item->data(Qt::UserRole).toInt();
    if (m_choice == CANCEL_ID) {
        reject();
    } else {
        accept();
    }
}

int SelectDialog::getSelection() const
{
    return m_choice;
}

void SelectDialog::updateSize()
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

void SelectDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    updateSize();
}

void SelectDialog::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(m_list, p_event, this)) {
        return;
    }

    // On Mac OS X, it is `Command+O` to activate an item, instead of Return.
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    int key = p_event->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        p_event->accept();
        if (auto item = m_list->currentItem()) {
            selectionChosen(item);
        }

        return;
    }
#endif

    QDialog::keyPressEvent(p_event);
}
