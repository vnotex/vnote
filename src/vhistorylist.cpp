#include "vhistorylist.h"

#include <QtWidgets>

#include "utils/viconutils.h"
#include "utils/vutils.h"
#include "vlistwidget.h"
#include "vconfigmanager.h"
#include "vmainwindow.h"
#include "vcart.h"
#include "vnote.h"
#include "vnotefile.h"
#include "vdirectory.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;

extern VNote *g_vnote;

VHistoryList::VHistoryList(QWidget *p_parent)
    : QWidget(p_parent),
      m_initialized(false),
      m_uiInitialized(false),
      m_updatePending(true),
      m_currentDate(QDate::currentDate())
{
}

void VHistoryList::setupUI()
{
    if (m_uiInitialized) {
        return;
    }

    m_uiInitialized = true;

    m_clearBtn = new QPushButton(VIconUtils::buttonDangerIcon(":/resources/icons/clear_history.svg"), "");
    m_clearBtn->setToolTip(tr("Clear"));
    m_clearBtn->setProperty("FlatBtn", true);
    connect(m_clearBtn, &QPushButton::clicked,
            this, [this]() {
                init();

                if (m_histories.size() > 0) {
                    int ret = VUtils::showMessage(QMessageBox::Warning,
                                                  tr("Warning"),
                                                  tr("Are you sure to clear History?"),
                                                  "",
                                                  QMessageBox::Ok | QMessageBox::Cancel,
                                                  QMessageBox::Ok,
                                                  g_mainWin,
                                                  MessageBoxType::Danger);
                    if (ret == QMessageBox::Ok) {
                        m_histories.clear();
                        g_config->setHistory(m_histories);
                        m_updatePending = true;
                        updateList();
                    }
                }
            });

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    btnLayout->setContentsMargins(0, 0, 0, 0);

    m_itemList = new VListWidget();
    m_itemList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_itemList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_itemList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_itemList, &QListWidget::customContextMenuRequested,
            this, &VHistoryList::handleContextMenuRequested);
    connect(m_itemList, &QListWidget::itemActivated,
            this, &VHistoryList::openItem);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_itemList);
    mainLayout->setContentsMargins(3, 0, 3, 0);

    setLayout(mainLayout);
}

void VHistoryList::addFile(const QString &p_filePath)
{
    init();

    QStringList files;
    files << p_filePath;
    addFilesInternal(files, false);

    if (isVisible()) {
        updateList();
    }
}

void VHistoryList::pinFiles(const QStringList &p_files)
{
    init();

    addFilesInternal(p_files, true);
    if (isVisible()) {
        updateList();
    }
}

void VHistoryList::unpinFiles(const QStringList &p_files)
{
    init();

    for (auto const & file : p_files) {
        auto it = findFileInHistory(file);
        if (it != m_histories.end()) {
            it->m_isPinned = false;
        }
    }

    g_config->setHistory(m_histories);
    m_updatePending = true;
}

void VHistoryList::pinFolder(const QString &p_folder)
{
    init();

    auto it = findFileInHistory(p_folder);
    if (it != m_histories.end()) {
        return;
    }

    m_histories.append(VHistoryEntry(p_folder, QDate::currentDate(), true, true));

    checkHistorySize();

    g_config->setHistory(m_histories);
    m_updatePending = true;

    if (isVisible()) {
        updateList();
    }
}

void VHistoryList::addFilesInternal(const QStringList &p_files, bool p_isPinned)
{
    for (auto const & file : p_files) {
        // Find it in existing entries.
        bool pinnedBefore = false;
        auto it = findFileInHistory(file);
        if (it != m_histories.end()) {
            pinnedBefore = it->m_isPinned;
            m_histories.erase(it);
        }

        // Append an entry at the end.
        bool pin = p_isPinned ? true : (pinnedBefore ? true : false);
        m_histories.append(VHistoryEntry(file, QDate::currentDate(), pin));
    }

    checkHistorySize();

    g_config->setHistory(m_histories);
    m_updatePending = true;
}

void VHistoryList::checkHistorySize()
{
    int numToRemove = m_histories.size() - g_config->getHistorySize();
    for (auto rit = m_histories.begin(); numToRemove > 0 && rit != m_histories.end();) {
        if (rit->m_isPinned) {
            ++rit;
            continue;
        }

        rit = m_histories.erase(rit);
        --numToRemove;
    }
}

void VHistoryList::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    setupUI();

    g_config->getHistory(m_histories);

    m_updatePending = true;
}

void VHistoryList::showEvent(QShowEvent *p_event)
{
    init();

    if (m_currentDate != QDate::currentDate()) {
        m_currentDate = QDate::currentDate();
        m_updatePending = true;
    }

    updateList();

    QWidget::showEvent(p_event);
}

QLinkedList<VHistoryEntry>::iterator VHistoryList::findFileInHistory(const QString &p_file)
{
    for (QLinkedList<VHistoryEntry>::iterator it = m_histories.begin();
         it != m_histories.end();
         ++it) {
        if (VUtils::equalPath(p_file, it->m_file)) {
            return it;
        }
    }

    return m_histories.end();
}

struct SeparatorItem
{
    SeparatorItem()
        : m_valid(false)
    {
    }

    QString m_date;
    QListWidgetItem *m_item;
    bool m_valid;
};

void VHistoryList::updateList()
{
    if (!m_updatePending) {
        return;
    }

    m_updatePending = false;

    m_itemList->clearAll();

    if (m_histories.isEmpty()) {
        return;
    }

    // Add separators.
    // Pinned separator.
    SeparatorItem pinItem;
    pinItem.m_item = VListWidget::createSeparatorItem(tr("Pinned"));
    m_itemList->addItem(pinItem.m_item);

    const int sepSize = 4;
    QString sepText[4] = {tr("Today"), tr("Yesterday"), tr("Last 7 Days"), tr("Older")};
    QVector<SeparatorItem> seps(sepSize);

    seps[0].m_date = m_currentDate.toString(Qt::ISODate);
    seps[1].m_date = m_currentDate.addDays(-1).toString(Qt::ISODate);
    seps[2].m_date = m_currentDate.addDays(-7).toString(Qt::ISODate);
    // Leave the last string empty.

    for (int i = 0; i < sepSize; ++i) {
        seps[i].m_item = VListWidget::createSeparatorItem(sepText[i]);
        m_itemList->addItem(seps[i].m_item);
    }

    QIcon noteIcon(VIconUtils::treeViewIcon(":/resources/icons/note_item.svg"));
    QIcon folderIcon(VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg"));
    for (auto it = m_histories.cbegin(); it != m_histories.cend(); ++it) {
        QListWidgetItem *item = new QListWidgetItem(VUtils::fileNameFromPath(it->m_file));
        item->setToolTip(it->m_file);
        item->setData(Qt::UserRole, (qulonglong)&(*it));

        if (it->m_isFolder) {
            item->setIcon(folderIcon);
        } else {
            item->setIcon(noteIcon);
        }

        if (it->m_isPinned) {
            m_itemList->insertItem(m_itemList->row(pinItem.m_item) + 1, item);
            pinItem.m_valid = true;
            continue;
        }

        for (int i = 0; i < sepSize; ++i) {
            if (it->m_date >= seps[i].m_date) {
                m_itemList->insertItem(m_itemList->row(seps[i].m_item) + 1, item);
                seps[i].m_valid = true;
                break;
            }
        }
    }

    // We always display pinned separator.

    for (int i = 0; i < sepSize; ++i) {
        if (!seps[i].m_valid) {
            delete seps[i].m_item;
        }
    }

    seps.clear();
}

void VHistoryList::handleContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_itemList->itemAt(p_pos);
    if (!item || VListWidget::isSeparatorItem(item)) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QAction *openAct = new QAction(tr("&Open"), &menu);
    openAct->setToolTip(tr("Open selected notes"));
    connect(openAct, &QAction::triggered,
            this, &VHistoryList::openSelectedItems);
    menu.addAction(openAct);

    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();
    if (selectedItems.size() == 1) {
        QAction *locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                                         tr("&Locate To Folder"),
                                         &menu);
        locateAct->setToolTip(tr("Locate the folder of current note"));
        connect(locateAct, &QAction::triggered,
                this, &VHistoryList::locateCurrentItem);
        menu.addAction(locateAct);
    }

    bool allPinned = true, allUnpinned = true;
    for (auto const & it : selectedItems) {
        if (getHistoryEntry(it)->m_isPinned) {
            allUnpinned = false;
        } else {
            allPinned = false;
        }
    }

    if (allUnpinned) {
        QAction *pinAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                      tr("Pin"),
                                      &menu);
        pinAct->setToolTip(tr("Pin selected notes in History"));
        connect(pinAct, &QAction::triggered,
                this, &VHistoryList::pinSelectedItems);
        menu.addAction(pinAct);
    } else if (allPinned) {
        QAction *unpinAct = new QAction(tr("Unpin"), &menu);
        unpinAct->setToolTip(tr("Unpin selected notes in History"));
        connect(unpinAct, &QAction::triggered,
                this, &VHistoryList::unpinSelectedItems);
        menu.addAction(unpinAct);
    }

    menu.addSeparator();

    QAction *addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                        tr("Add To Cart"),
                                        &menu);
    addToCartAct->setToolTip(tr("Add selected notes to Cart for further processing"));
    connect(addToCartAct, &QAction::triggered,
            this, &VHistoryList::addFileToCart);
    menu.addAction(addToCartAct);

    menu.exec(m_itemList->mapToGlobal(p_pos));
}

bool VHistoryList::isFolder(const QListWidgetItem *p_item) const
{
    return getHistoryEntry(p_item)->m_isFolder;
}

VHistoryEntry *VHistoryList::getHistoryEntry(const QListWidgetItem *p_item) const
{
    return (VHistoryEntry *)p_item->data(Qt::UserRole).toULongLong();
}

QString VHistoryList::getFilePath(const QListWidgetItem *p_item) const
{
    return getHistoryEntry(p_item)->m_file;
}

void VHistoryList::pinSelectedItems()
{
    QStringList files;
    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();
    for (auto it : selectedItems) {
        files << getFilePath(it);
    }

    pinFiles(files);
}

void VHistoryList::unpinSelectedItems()
{
    QStringList files;
    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();
    for (auto it : selectedItems) {
        files << getFilePath(it);
    }

    unpinFiles(files);

    if (isVisible()) {
        updateList();
    }
}

void VHistoryList::openSelectedItems() const
{
    QStringList files;
    QList<QListWidgetItem *> selectedItems = m_itemList->selectedItems();

    if (selectedItems.size() == 1 && isFolder(selectedItems.first())) {
        // Locate to the folder.
        VDirectory *dir = g_vnote->getInternalDirectory(getFilePath(selectedItems.first()));
        if (dir) {
            g_mainWin->locateDirectory(dir);
        }

        return;
    }

    for (auto it : selectedItems) {
        if (isFolder(it)) {
            // Skip folders.
            continue;
        }

        files << getFilePath(it);
    }

    if (!files.isEmpty()) {
        g_mainWin->openFiles(files);
    }
}

void VHistoryList::openItem(const QListWidgetItem *p_item) const
{
    if (!p_item) {
        return;
    }

    if (isFolder(p_item)) {
        // Locate to the folder.
        VDirectory *dir = g_vnote->getInternalDirectory(getFilePath(p_item));
        if (dir) {
            g_mainWin->locateDirectory(dir);
        }

        return;
    }

    QStringList files;
    files << getFilePath(p_item);
    g_mainWin->openFiles(files);
}

void VHistoryList::locateCurrentItem()
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

void VHistoryList::showNavigation()
{
    setupUI();

    VNavigationMode::showNavigation(m_itemList);
}

bool VHistoryList::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    setupUI();

    return VNavigationMode::handleKeyNavigation(m_itemList,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

void VHistoryList::addFileToCart() const
{
    QList<QListWidgetItem *> items = m_itemList->selectedItems();
    VCart *cart = g_mainWin->getCart();

    for (int i = 0; i < items.size(); ++i) {
        cart->addFile(getFilePath(items[i]));
    }

    g_mainWin->showStatusMessage(tr("%1 %2 added to Cart")
                                   .arg(items.size())
                                   .arg(items.size() > 1 ? tr("notes") : tr("note")));
}

void VHistoryList::focusInEvent(QFocusEvent *p_event)
{
    init();

    QWidget::focusInEvent(p_event);
    m_itemList->setFocus();
}
