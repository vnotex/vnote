#include "vcart.h"

#include <QtWidgets>

#include "utils/viconutils.h"
#include "utils/vutils.h"
#include "vmainwindow.h"
#include "vnote.h"
#include "vnotefile.h"
#include "vlistwidget.h"
#include "dialog/vsortdialog.h"

extern VMainWindow *g_mainWin;

extern VNote *g_vnote;

VCart::VCart(QWidget *p_parent)
    : QWidget(p_parent),
      m_initialized(false),
      m_uiInitialized(false)
{
}

void VCart::setupUI()
{
    if (m_uiInitialized) {
        return;
    }

    m_uiInitialized = true;

    m_clearBtn = new QPushButton(VIconUtils::buttonDangerIcon(":/resources/icons/clear_cart.svg"), "");
    m_clearBtn->setToolTip(tr("Clear"));
    m_clearBtn->setProperty("FlatBtn", true);
    connect(m_clearBtn, &QPushButton::clicked,
            this, [this]() {
                if (m_itemList->count() > 0) {
                    int ret = VUtils::showMessage(QMessageBox::Warning,
                                                  tr("Warning"),
                                                  tr("Are you sure to clear Cart?"),
                                                  "",
                                                  QMessageBox::Ok | QMessageBox::Cancel,
                                                  QMessageBox::Ok,
                                                  g_mainWin,
                                                  MessageBoxType::Danger);
                    if (ret == QMessageBox::Ok) {
                        m_itemList->clearAll();
                        updateNumberLabel();
                    }
                }
            });

    m_numLabel = new QLabel();

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_numLabel);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    m_itemList = new VListWidget();
    m_itemList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_itemList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_itemList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_itemList->setDragDropMode(QAbstractItemView::InternalMove);
    connect(m_itemList, &QListWidget::customContextMenuRequested,
            this, &VCart::handleContextMenuRequested);
    connect(m_itemList, &QListWidget::itemActivated,
            this, &VCart::openItem);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_itemList);
    mainLayout->setContentsMargins(3, 0, 3, 0);

    setLayout(mainLayout);
}

void VCart::handleContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_itemList->itemAt(p_pos);
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (item) {
        QAction *openAct = new QAction(tr("&Open"), &menu);
        openAct->setToolTip(tr("Open selected notes"));
        connect(openAct, &QAction::triggered,
                this, &VCart::openSelectedItems);
        menu.addAction(openAct);

        if (m_itemList->selectedItems().size() == 1) {
            QAction *locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                                             tr("&Locate To Folder"),
                                             &menu);
            locateAct->setToolTip(tr("Locate the folder of current note"));
            connect(locateAct, &QAction::triggered,
                    this, &VCart::locateCurrentItem);
            menu.addAction(locateAct);
        }

        QAction *deleteAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/delete_cart_item.svg"),
                                         tr("&Delete"),
                                         &menu);
        deleteAct->setToolTip(tr("Delete selected items from Cart"));
        connect(deleteAct, &QAction::triggered,
                this, &VCart::deleteSelectedItems);
        menu.addAction(deleteAct);
    }

    if (m_itemList->count() == 0) {
        return;
    }

    if (!menu.actions().isEmpty()) {
        menu.addSeparator();
    }

    QAction *sortAct = new QAction(VIconUtils::menuIcon(":/resources/icons/sort.svg"),
                                   tr("&Sort"),
                                   &menu);
    sortAct->setToolTip(tr("Sort items in Cart"));
    connect(sortAct, &QAction::triggered,
            this, &VCart::sortItems);
    menu.addAction(sortAct);

    menu.exec(m_itemList->mapToGlobal(p_pos));
}

void VCart::addFile(const QString &p_filePath)
{
    init();

    if (p_filePath.isEmpty()
        || findFileInCart(p_filePath) != -1) {
        return;
    }

    addItem(p_filePath);
}

int VCart::findFileInCart(const QString &p_file) const
{
    int cnt = m_itemList->count();
    for (int i = 0; i < cnt; ++i) {
        if (VUtils::equalPath(m_itemList->item(i)->data(Qt::UserRole).toString(),
                              p_file)) {
            return i;
        }
    }

    return -1;
}

void VCart::addItem(const QString &p_path)
{
    QListWidgetItem *item = new QListWidgetItem(VUtils::fileNameFromPath(p_path));
    item->setToolTip(p_path);
    item->setData(Qt::UserRole, p_path);

    m_itemList->addItem(item);
    updateNumberLabel();
}

void VCart::deleteSelectedItems()
{
    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();
    for (auto it : selectedItems) {
        delete it;
    }
}

void VCart::openSelectedItems() const
{
    QStringList files;
    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();
    for (auto it : selectedItems) {
        files << getFilePath(it);
    }

    if (!files.isEmpty()) {
        g_mainWin->openFiles(files);
    }
}

void VCart::locateCurrentItem()
{
    auto item = m_itemList->currentItem();
    if (!item) {
        return;
    }

    VFile *file = g_vnote->getInternalFile(getFilePath(item));
    if (file) {
        g_mainWin->locateFile(file);
    }
}

void VCart::openItem(const QListWidgetItem *p_item) const
{
    if (!p_item) {
        return;
    }

    QStringList files;
    files << getFilePath(p_item);
    g_mainWin->openFiles(files);
}

QString VCart::getFilePath(const QListWidgetItem *p_item) const
{
    return p_item->data(Qt::UserRole).toString();
}

int VCart::count() const
{
    const_cast<VCart *>(this)->init();

    return m_itemList->count();
}

QVector<QString> VCart::getFiles() const
{
    const_cast<VCart *>(this)->init();

    QVector<QString> files;
    int cnt = m_itemList->count();
    for (int i = 0; i < cnt; ++i) {
        files.append(getFilePath(m_itemList->item(i)));
    }

    return files;
}

void VCart::sortItems()
{
    if (m_itemList->count() < 2) {
        return;
    }

    VSortDialog dialog(tr("Sort Cart"),
                       tr("Sort items in Cart."),
                       this);
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(1);
    QStringList headers(tr("Name"));
    tree->setHeaderLabels(headers);

    int cnt = m_itemList->count();
    for (int i = 0; i < cnt; ++i) {
        QListWidgetItem *it = m_itemList->item(i);
        QTreeWidgetItem *item = new QTreeWidgetItem(tree,
                                                    QStringList(it->text()));
        item->setToolTip(0, getFilePath(it));
        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        QVector<QVariant> data = dialog.getSortedData();
        Q_ASSERT(data.size() == cnt);
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }

        VListWidget::sortListWidget(m_itemList, sortedIdx);
    }
}

void VCart::updateNumberLabel() const
{
    int cnt = m_itemList->count();
    m_numLabel->setText(tr("%1 %2").arg(cnt)
                                   .arg(cnt > 1 ? tr("Items") : tr("Item")));
}

void VCart::showNavigation()
{
    setupUI();

    VNavigationMode::showNavigation(m_itemList);
}

bool VCart::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    setupUI();

    return VNavigationMode::handleKeyNavigation(m_itemList,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

void VCart::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    setupUI();
    updateNumberLabel();
}

void VCart::showEvent(QShowEvent *p_event)
{
    init();

    QWidget::showEvent(p_event);
}

void VCart::focusInEvent(QFocusEvent *p_event)
{
    init();

    QWidget::focusInEvent(p_event);
    m_itemList->setFocus();
}
