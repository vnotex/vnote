#include "vcart.h"

#include <QtWidgets>

#include "utils/viconutils.h"
#include "utils/vutils.h"
#include "vmainwindow.h"
#include "vnote.h"
#include "vnotefile.h"

extern VMainWindow *g_mainWin;

extern VNote *g_vnote;

VCart::VCart(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();

    initActions();
}

void VCart::setupUI()
{
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
                        m_itemList->clear();
                    }
                }
            });

    m_numLabel = new QLabel();

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_numLabel);
    btnLayout->setContentsMargins(0, 0, 3, 0);

    m_itemList = new QListWidget();
    m_itemList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_itemList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_itemList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_itemList, &QListWidget::customContextMenuRequested,
            this, &VCart::handleContextMenuRequested);
    connect(m_itemList, &QListWidget::itemActivated,
            this, &VCart::openItem);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_itemList);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

void VCart::initActions()
{
    m_openAct = new QAction(tr("&Open"), this);
    m_openAct->setToolTip(tr("Open selected notes"));
    connect(m_openAct, &QAction::triggered,
            this, &VCart::openSelectedItems);

    m_locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                              tr("Locate To Folder"),
                              this);
    m_locateAct->setToolTip(tr("Locate the folder of current note"));
    connect(m_locateAct, &QAction::triggered,
            this, &VCart::locateCurrentItem);

    m_deleteAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/delete_cart_item.svg"),
                              tr("&Delete"),
                              this);
    m_deleteAct->setToolTip(tr("Delete selected items from Cart"));
    connect(m_deleteAct, &QAction::triggered,
            this, &VCart::deleteSelectedItems);
}

void VCart::handleContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_itemList->itemAt(p_pos);
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (item) {
        int itemCount = m_itemList->selectedItems().size();
        if (itemCount == 1) {
            menu.addAction(m_openAct);
            menu.addAction(m_locateAct);
        }

        menu.addAction(m_deleteAct);
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(m_itemList->mapToGlobal(p_pos));
    }
}

void VCart::addFile(const QString &p_filePath)
{
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

    int cnt = m_itemList->count();
    if (cnt > 0) {
        m_numLabel->setText(tr("%1 %2").arg(cnt)
                                       .arg(cnt > 1 ? tr("Items") : tr("Item")));
    } else {
        m_numLabel->setText("");
    }
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
    openItem(m_itemList->currentItem());
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
    return m_itemList->count();
}

QVector<QString> VCart::getFiles() const
{
    QVector<QString> files;
    int cnt = m_itemList->count();
    for (int i = 0; i < cnt; ++i) {
        files.append(getFilePath(m_itemList->item(i)));
    }

    return files;
}
