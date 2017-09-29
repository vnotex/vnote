#include "vsortdialog.h"

#include <QtWidgets>

void VTreeWidget::dropEvent(QDropEvent *p_event)
{
    QList<QTreeWidgetItem *> dragItems = selectedItems();

    int first = -1, last = -1;
    QTreeWidgetItem *firstItem = NULL;
    for (int i = 0; i < dragItems.size(); ++i) {
        int row = indexFromItem(dragItems[i]).row();
        if (row > last) {
            last = row;
        }

        if (first == -1 || row < first) {
            first = row;
            firstItem = dragItems[i];
        }
    }

    Q_ASSERT(firstItem);

    QTreeWidget::dropEvent(p_event);

    int target = indexFromItem(firstItem).row();
    emit rowsMoved(first, last, target);
}


VSortDialog::VSortDialog(const QString &p_title,
                         const QString &p_info,
                         QWidget *p_parent)
    : QDialog(p_parent)
{
    setupUI(p_title, p_info);
}

void VSortDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    m_treeWidget = new VTreeWidget();
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
    connect(m_treeWidget, &VTreeWidget::rowsMoved,
            this, [this](int p_first, int p_last, int p_row) {
                Q_UNUSED(p_first);
                Q_UNUSED(p_last);
                QTreeWidgetItem *item = m_treeWidget->topLevelItem(p_row);
                if (item) {
                    m_treeWidget->setCurrentItem(item);

                    // Select all items back.
                    int cnt = p_last - p_first + 1;
                    for (int i = 0; i < cnt; ++i) {
                        QTreeWidgetItem *it = m_treeWidget->topLevelItem(p_row + i);
                        if (it) {
                            it->setSelected(true);
                        }
                    }
                }
            });

    // Buttons for top/up/down/bottom.
    m_topBtn = new QPushButton(tr("&Top"));
    m_topBtn->setToolTip(tr("Move selected items to top"));
    connect(m_topBtn, &QPushButton::clicked,
            this, [this]() {
                this->handleMoveOperation(MoveOperation::Top);
            });

    m_upBtn = new QPushButton(tr("&Up"));
    m_upBtn->setToolTip(tr("Move selected items up"));
    connect(m_upBtn, &QPushButton::clicked,
            this, [this]() {
                this->handleMoveOperation(MoveOperation::Up);
            });

    m_downBtn = new QPushButton(tr("&Down"));
    m_downBtn->setToolTip(tr("Move selected items down"));
    connect(m_downBtn, &QPushButton::clicked,
            this, [this]() {
                this->handleMoveOperation(MoveOperation::Down);
            });

    m_bottomBtn = new QPushButton(tr("&Bottom"));
    m_bottomBtn->setToolTip(tr("Move selected items to bottom"));
    connect(m_bottomBtn, &QPushButton::clicked,
            this, [this]() {
                this->handleMoveOperation(MoveOperation::Bottom);
            });

    QVBoxLayout *btnLayout = new QVBoxLayout;
    btnLayout->addWidget(m_topBtn);
    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    btnLayout->addWidget(m_bottomBtn);
    btnLayout->addStretch();

    QHBoxLayout *midLayout = new QHBoxLayout;
    midLayout->addWidget(m_treeWidget);
    midLayout->addLayout(btnLayout);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(midLayout);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    setWindowTitle(p_title);
}

void VSortDialog::treeUpdated()
{
    int cols = m_treeWidget->columnCount();
    for (int i = 0; i < cols; ++i) {
        m_treeWidget->resizeColumnToContents(i);
    }

    // We just need single level.
    int cnt = m_treeWidget->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        QTreeWidgetItem *item = m_treeWidget->topLevelItem(i);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
}

void VSortDialog::handleMoveOperation(MoveOperation p_op)
{
    const QList<QTreeWidgetItem *> selectedItems = m_treeWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    int first = m_treeWidget->topLevelItemCount();
    int last = -1;
    for (auto const & it : selectedItems) {
        int idx = m_treeWidget->indexOfTopLevelItem(it);
        Q_ASSERT(idx > -1);
        if (idx < first) {
            first = idx;
        }

        if (idx > last) {
            last = idx;
        }
    }

    Q_ASSERT(first <= last && (last - first + 1) == selectedItems.size());
    QTreeWidgetItem *firstItem = NULL;

    switch (p_op) {
    case MoveOperation::Top:
        if (first == 0) {
            break;
        }

        m_treeWidget->clearSelection();

        // Insert item[last] to index 0 repeatedly.
        for (int i = last - first; i >= 0; --i) {
            QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(last);
            Q_ASSERT(item);
            m_treeWidget->insertTopLevelItem(0, item);
            item->setSelected(true);
        }

        firstItem = m_treeWidget->topLevelItem(0);

        break;

    case MoveOperation::Up:
        if (first == 0) {
            break;
        }

        m_treeWidget->clearSelection();

        // Insert item[last] to index (first -1) repeatedly.
        for (int i = last - first; i >= 0; --i) {
            QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(last);
            Q_ASSERT(item);
            m_treeWidget->insertTopLevelItem(first - 1, item);
            item->setSelected(true);
        }

        firstItem = m_treeWidget->topLevelItem(first - 1);

        break;

    case MoveOperation::Down:
        if (last == m_treeWidget->topLevelItemCount() - 1) {
            break;
        }

        m_treeWidget->clearSelection();

        // Insert item[first] to index (last) repeatedly.
        for (int i = last - first; i >= 0; --i) {
            QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(first);
            Q_ASSERT(item);
            m_treeWidget->insertTopLevelItem(last + 1, item);
            item->setSelected(true);

            if (!firstItem) {
                firstItem = item;
            }
        }

        break;

    case MoveOperation::Bottom:
        if (last == m_treeWidget->topLevelItemCount() - 1) {
            break;
        }

        m_treeWidget->clearSelection();

        // Insert item[first] to the last of the tree repeatedly.
        for (int i = last - first; i >= 0; --i) {
            QTreeWidgetItem *item = m_treeWidget->takeTopLevelItem(first);
            Q_ASSERT(item);
            m_treeWidget->addTopLevelItem(item);
            item->setSelected(true);

            if (!firstItem) {
                firstItem = item;
            }
        }

        break;

    default:
        return;
    }

    if (firstItem) {
        m_treeWidget->setCurrentItem(firstItem);
        m_treeWidget->scrollToItem(firstItem);
    }
}

QVector<QVariant> VSortDialog::getSortedData() const
{
    int cnt = m_treeWidget->topLevelItemCount();
    QVector<QVariant> data(cnt);
    for (int i = 0; i < cnt; ++i) {
        QTreeWidgetItem *item = m_treeWidget->topLevelItem(i);
        Q_ASSERT(item);
        data[i] = item->data(0, Qt::UserRole);
    }

    return data;
}
