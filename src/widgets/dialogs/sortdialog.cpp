#include "sortdialog.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>

#include <widgets/treewidget.h>
#include <widgets/widgetsfactory.h>
#include <widgets/treewidgetitem.h>
#include <core/global.h>

using namespace vnotex;

SortDialog::SortDialog(const QString &p_title,
                       const QString &p_info,
                       QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI(p_title, p_info);
}

void SortDialog::setupUI(const QString &p_title, const QString &p_info)
{
    auto mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    auto mainLayout = new QVBoxLayout(mainWidget);

    if (!p_info.isEmpty()) {
        auto infoLabel = new QLabel(p_info, mainWidget);
        infoLabel->setWordWrap(true);
        mainLayout->addWidget(infoLabel);
    }

    {
        auto bodyLayout = new QHBoxLayout();
        mainLayout->addLayout(bodyLayout);

        // Tree widget.
        // We want to sort it case-insensitive. QTreeView and QSortFilterProxyModel should be the choice.
        // For simplicity, we subclass QTreeWidgetItem here.
        m_treeWidget = new TreeWidget(mainWidget);
        m_treeWidget->setRootIsDecorated(false);
        m_treeWidget->setSelectionMode(QAbstractItemView::ContiguousSelection);
        m_treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
        bodyLayout->addWidget(m_treeWidget);

        // Buttons for top/up/down/bottom.
        auto btnLayout = new QVBoxLayout();
        bodyLayout->addLayout(btnLayout);

        auto topBtn = new QPushButton(tr("&Top"), mainWidget);
        connect(topBtn, &QPushButton::clicked,
                this, [this]() {
                    handleMoveOperation(MoveOperation::Top);
                });
        btnLayout->addWidget(topBtn);

        auto upBtn = new QPushButton(tr("&Up"), mainWidget);
        connect(upBtn, &QPushButton::clicked,
                this, [this]() {
                    handleMoveOperation(MoveOperation::Up);
                });
        btnLayout->addWidget(upBtn);

        auto downBtn = new QPushButton(tr("&Down"), mainWidget);
        connect(downBtn, &QPushButton::clicked,
                this, [this]() {
                    handleMoveOperation(MoveOperation::Down);
                });
        btnLayout->addWidget(downBtn);

        auto bottomBtn = new QPushButton(tr("&Bottom"), mainWidget);
        connect(bottomBtn, &QPushButton::clicked,
                this, [this]() {
                    handleMoveOperation(MoveOperation::Bottom);
                });
        btnLayout->addWidget(bottomBtn);

        btnLayout->addStretch();
    }

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    setWindowTitle(p_title);
}

QTreeWidget *SortDialog::getTreeWidget() const
{
    return m_treeWidget;
}

void SortDialog::updateTreeWidget()
{
    int cols = m_treeWidget->columnCount();
    for (int i = 0; i < cols; ++i) {
        m_treeWidget->resizeColumnToContents(i);
    }

    QHeaderView *header = m_treeWidget->header();
    if (header) {
        header->setStretchLastSection(true);
    }

    // We just need single level.
    int cnt = m_treeWidget->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        QTreeWidgetItem *item = m_treeWidget->topLevelItem(i);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }

    m_treeWidget->sortByColumn(-1, Qt::AscendingOrder);
    m_treeWidget->setSortingEnabled(true);
}

QVector<QVariant> SortDialog::getSortedData() const
{
    const int cnt = m_treeWidget->topLevelItemCount();
    QVector<QVariant> data(cnt);
    for (int i = 0; i < cnt; ++i) {
        QTreeWidgetItem *item = m_treeWidget->topLevelItem(i);
        Q_ASSERT(item);
        data[i] = item->data(0, Qt::UserRole);
    }

    return data;
}

void SortDialog::handleMoveOperation(MoveOperation p_op)
{
    const QList<QTreeWidgetItem *> selectedItems = m_treeWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    int first = m_treeWidget->topLevelItemCount();
    int last = -1;
    for (const auto &it : selectedItems) {
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
    QTreeWidgetItem *firstItem = nullptr;

    m_treeWidget->sortByColumn(-1, Qt::AscendingOrder);

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

QTreeWidgetItem *SortDialog::addItem(const QStringList &p_cols)
{
    auto item = new TreeWidgetItem(m_treeWidget, p_cols);
    return item;
}

QTreeWidgetItem *SortDialog::addItem(const QStringList &p_cols, const QStringList &p_comparisonCols)
{
    Q_ASSERT(p_cols.size() == p_comparisonCols.size());
    auto item = new TreeWidgetItem(m_treeWidget, p_cols);
    for (int i = 0; i < p_cols.size(); ++i) {
        if (!p_comparisonCols[i].isNull()) {
            item->setData(i, Role::ComparisonRole, p_comparisonCols[i]);
        }
    }
    return item;
}
