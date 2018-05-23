#include <QtWidgets>
#include <QtDebug>

#include "veditwindow.h"
#include "vedittab.h"
#include "utils/vutils.h"
#include "vorphanfile.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vopenedlistmenu.h"
#include "vmdtab.h"
#include "vhtmltab.h"
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "utils/viconutils.h"
#include "vcart.h"
#include "vhistorylist.h"

extern VConfigManager *g_config;
extern VMainWindow *g_mainWin;

#define GET_TAB_FROM_SENDER() static_cast<QAction *>(sender())->data().toInt()

VEditWindow::VEditWindow(VEditArea *editArea, QWidget *parent)
    : QTabWidget(parent),
      m_editArea(editArea),
      m_curTabWidget(NULL),
      m_lastTabWidget(NULL),
      m_removeSplitAct(NULL)
{
    setAcceptDrops(true);
    setupCornerWidget();

    // Explicit speficy in macOS.
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideRight);
    setTabsClosable(true);
    setMovable(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    QTabBar *bar = tabBar();
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bar, &QTabBar::customContextMenuRequested,
            this, &VEditWindow::tabbarContextMenuRequested);

    connect(this, &VEditWindow::tabCloseRequested,
            this, &VEditWindow::closeTab);
    connect(this, &VEditWindow::tabBarClicked,
            this, &VEditWindow::handleTabbarClicked);

    connect(this, &VEditWindow::tabBarDoubleClicked,
            this, [this](int p_index) {
                if (p_index != -1 && g_config->getDoubleClickCloseTab()) {
                    closeTab(p_index);
                }
            });

    connect(this, &VEditWindow::currentChanged,
            this, &VEditWindow::handleCurrentIndexChanged);
    connect(this, &VEditWindow::customContextMenuRequested,
            this, &VEditWindow::contextMenuRequested);
}

void VEditWindow::setupCornerWidget()
{
    // Left button
    leftBtn = new QPushButton(VIconUtils::editWindowCornerIcon(":/resources/icons/corner_tablist.svg"),
                              "", this);
    leftBtn->setProperty("CornerBtn", true);
    leftBtn->setToolTip(tr("Opened Notes List"));
    QString keyText = VUtils::getCaptainShortcutSequenceText("OpenedFileList");
    if (!keyText.isEmpty()) {
        leftBtn->setToolTip(QString("%1\t%2").arg(leftBtn->toolTip()).arg(keyText));
    }

    VOpenedListMenu *leftMenu = new VOpenedListMenu(this);
    leftMenu->setToolTipsVisible(true);
    connect(leftMenu, &VOpenedListMenu::fileTriggered,
            this, &VEditWindow::tabListJump);
    leftBtn->setMenu(leftMenu);

    // Right button
    rightBtn = new QPushButton(VIconUtils::editWindowCornerIcon(":/resources/icons/corner_menu.svg"),
                               "", this);
    rightBtn->setProperty("CornerBtn", true);
    rightBtn->setToolTip(tr("Menu"));
    QMenu *rightMenu = new QMenu(this);
    rightMenu->setToolTipsVisible(true);
    rightBtn->setMenu(rightMenu);
    connect(rightMenu, &QMenu::aboutToShow,
            this, [this, rightMenu]() {
                updateSplitMenu(rightMenu);
            });

    // Move all buttons to the right corner.
    QWidget *widget = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(leftBtn);
    layout->addWidget(rightBtn);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    widget->setLayout(layout);

    setCornerWidget(widget, Qt::TopRightCorner);
}

void VEditWindow::splitWindow(bool p_right)
{
    emit requestSplitWindow(this, p_right);
}

void VEditWindow::removeSplit()
{
    // Close all the files one by one
    // If user do not want to close a file, just stop removing.
    // Otherwise, closeAllFiles() will emit requestRemoveSplit.
    closeAllFiles(false);
}

void VEditWindow::removeEditTab(int p_index)
{
    Q_ASSERT(p_index > -1 && p_index < tabBar()->count());

    VEditTab *editor = getTab(p_index);

    removeTab(p_index);

    delete editor;
}

int VEditWindow::insertEditTab(int p_index, VFile *p_file, QWidget *p_page)
{
    int idx = insertTab(p_index,
                        p_page,
                        p_file->getName());
    updateTabInfo(idx);
    return idx;
}

int VEditWindow::openFile(VFile *p_file, OpenFileMode p_mode)
{
    qDebug() << "open" << p_file->getName();
    // Find if it has been opened already
    int idx = findTabByFile(p_file);
    if (idx > -1) {
        goto out;
    }

    idx = openFileInTab(p_file, p_mode);

out:
    setCurrentIndex(idx);
    focusWindow();
    return idx;
}

// Return true if we closed the file actually
bool VEditWindow::closeFile(const VFile *p_file, bool p_forced)
{
    // Find if it has been opened already
    int idx = findTabByFile(p_file);
    if (idx == -1) {
        return false;
    }

    VEditTab *editor = getTab(idx);
    Q_ASSERT(editor);
    VEditTabInfo tabInfo = editor->fetchTabInfo();
    VFileSessionInfo info = VFileSessionInfo::fromEditTabInfo(&tabInfo);
    if (!p_forced) {
        setCurrentIndex(idx);
        updateTabStatus(idx);
    }

    // Even p_forced is true we need to delete unused images.
    bool ok = editor->closeFile(p_forced);
    if (ok) {
        removeEditTab(idx);

        m_editArea->recordClosedFile(info);
    }

    if (count() == 0) {
        emit requestRemoveSplit(this);
    }

    return ok;
}

bool VEditWindow::closeFile(const VDirectory *p_dir, bool p_forced)
{
    Q_ASSERT(p_dir);
    int i = 0;
    while (i < count()) {
        VEditTab *editor = getTab(i);
        QPointer<VFile> file = editor->getFile();
        if (p_dir->containsFile(file)) {
            if (!closeFile(file, p_forced)) {
                return false;
            }
            // Closed a file, so don't need to add i.
        } else {
            ++i;
        }
    }
    return true;
}

bool VEditWindow::closeFile(const VNotebook *p_notebook, bool p_forced)
{
    Q_ASSERT(p_notebook);
    int i = 0;
    while (i < count()) {
        VEditTab *editor = getTab(i);
        QPointer<VFile> file = editor->getFile();
        if (p_notebook->containsFile(file)) {
            if (!closeFile(file, p_forced)) {
                return false;
            }
            // Closed a file, so don't need to add i.
        } else {
            ++i;
        }
    }
    return true;
}

bool VEditWindow::closeAllFiles(bool p_forced)
{
    int nrTab = count();
    bool ret = true;
    for (int i = 0; i < nrTab; ++i) {
        VEditTab *editor = getTab(0);

        VEditTabInfo tabInfo = editor->fetchTabInfo();
        VFileSessionInfo info = VFileSessionInfo::fromEditTabInfo(&tabInfo);

        if (!p_forced) {
            setCurrentIndex(0);
            updateTabStatus(0);
        }

        // Even p_forced is true we need to delete unused images.
        bool ok = editor->closeFile(p_forced);
        if (ok) {
            removeEditTab(0);

            m_editArea->recordClosedFile(info);
        } else {
            ret = false;
            break;
        }
    }

    if (count() == 0) {
        emit requestRemoveSplit(this);
    }

    return ret;
}

int VEditWindow::openFileInTab(VFile *p_file, OpenFileMode p_mode)
{
    VEditTab *editor = NULL;
    switch (p_file->getDocType()) {
    case DocType::Markdown:
        editor = new VMdTab(p_file, m_editArea, p_mode, this);
        break;

    case DocType::Html:
        editor = new VHtmlTab(p_file, m_editArea, p_mode, this);
        break;

    default:
        V_ASSERT(false);
        break;
    }

    // Connect the signals.
    connectEditTab(editor);

    // Insert right after current tab.
    // VFileList will depends on this behavior.
    return insertEditTab(currentIndex() + 1, p_file, editor);
}

int VEditWindow::findTabByFile(const VFile *p_file) const
{
    int nrTabs = count();
    for (int i = 0; i < nrTabs; ++i) {
        if (getTab(i)->getFile() == p_file) {
            return i;
        }
    }
    return -1;
}

bool VEditWindow::closeTab(int p_index)
{
    VEditTab *editor = getTab(p_index);
    Q_ASSERT(editor);
    VEditTabInfo tabInfo = editor->fetchTabInfo();
    VFileSessionInfo info = VFileSessionInfo::fromEditTabInfo(&tabInfo);
    bool ok = editor->closeFile(false);
    if (ok) {
        removeEditTab(p_index);

        m_editArea->recordClosedFile(info);
    }

    // User clicks the close button. We should make this window
    // to be current window.
    emit getFocused();
    if (count() == 0) {
        emit requestRemoveSplit(this);
    }

    return ok;
}

void VEditWindow::readFile(bool p_discard)
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->readFile(p_discard);
}

void VEditWindow::saveAndReadFile()
{
    saveFile();
    readFile();
}

void VEditWindow::editFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->editFile();
}

void VEditWindow::saveFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->saveFile();
}

void VEditWindow::updateTabStatus(int p_index)
{
    if (p_index == -1) {
        p_index = currentIndex();
    }

    if (p_index == -1) {
        emit tabStatusUpdated(VEditTabInfo());
        emit outlineChanged(VTableOfContent());
        emit currentHeaderChanged(VHeaderPointer());
        return;
    }

    VEditTab *tab = getTab(p_index);
    emit tabStatusUpdated(tab->fetchTabInfo());
    emit outlineChanged(tab->getOutline());
    emit currentHeaderChanged(tab->getCurrentHeader());

    updateTabInfo(p_index);
}

void VEditWindow::updateTabInfo(int p_index)
{
    static QIcon editModified = VIconUtils::tabBarSpecialIcon(":/resources/icons/editing_modified.svg");
    static QIcon edit = VIconUtils::tabBarIcon(":/resources/icons/editing.svg");
    static QIcon readModified = VIconUtils::tabBarSpecialIcon(":/resources/icons/reading_modified.svg");
    static QIcon read = VIconUtils::tabBarIcon(":/resources/icons/reading.svg");

    VEditTab *editor = getTab(p_index);
    const VFile *file = editor->getFile();
    bool editMode = editor->isEditMode();

    setTabText(p_index, generateTabText(p_index, editor));
    setTabToolTip(p_index, generateTooltip(file));

    QIcon icon;
    if (editMode) {
        icon = editor->isModified() ? editModified : edit;
    } else {
        icon = editor->isModified() ? readModified : read;
    }

    setTabIcon(p_index, icon);
}

void VEditWindow::updateAllTabsSequence()
{
    for (int i = 0; i < count(); ++i) {
        VEditTab *editor = getTab(i);
        setTabText(i, generateTabText(i, editor));
    }
}

VTableOfContent VEditWindow::getOutline() const
{
    int idx = currentIndex();
    if (idx == -1) {
        return VTableOfContent();
    }

    return getTab(idx)->getOutline();
}

VHeaderPointer VEditWindow::getCurrentHeader() const
{
    int idx = currentIndex();
    if (idx == -1) {
        return VHeaderPointer();
    }

    return getTab(idx)->getCurrentHeader();
}

// Focus this windows. Try to focus current tab.
void VEditWindow::focusWindow()
{
    int idx = currentIndex();
    if (idx == -1) {
        setFocus();
        emit getFocused();
        return;
    }
    getTab(idx)->focusTab();
}

void VEditWindow::handleTabbarClicked(int p_index)
{
    // Only handle the case when (p_index == currentIndex()) here
    // because currentIndex() is not changed yet. If we focus window
    // now, we may change the current index forcely.
    if (p_index == currentIndex()) {
        focusWindow();
    }
}

void VEditWindow::handleCurrentIndexChanged(int p_index)
{
    focusWindow();

    QWidget *wid = widget(p_index);
    if (wid && (wid == m_curTabWidget)) {
        return;
    }

    m_lastTabWidget = m_curTabWidget;
    m_curTabWidget = wid;
}

void VEditWindow::mousePressEvent(QMouseEvent *event)
{
    focusWindow();
    QTabWidget::mousePressEvent(event);
}

QAction *VEditWindow::getRemoveSplitAction()
{
    if (!m_removeSplitAct) {
        m_removeSplitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/remove_split.svg"),
                                       tr("Remove split"),
                                       this);
        m_removeSplitAct->setToolTip(tr("Remove current split window"));
        VUtils::fixTextWithCaptainShortcut(m_removeSplitAct, "RemoveSplit");
        connect(m_removeSplitAct, &QAction::triggered,
                this, &VEditWindow::removeSplit);
    }

    return m_removeSplitAct;
}

void VEditWindow::contextMenuRequested(QPoint pos)
{
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (canRemoveSplit()) {
        menu.addAction(getRemoveSplitAction());
        menu.exec(this->mapToGlobal(pos));
    }
}

void VEditWindow::tabbarContextMenuRequested(QPoint p_pos)
{
    QTabBar *bar = tabBar();
    int tab = bar->tabAt(p_pos);
    if (tab == -1) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QAction *recycleBinAct = new QAction(VIconUtils::menuIcon(":/resources/icons/recycle_bin.svg"),
                                         tr("&Recycle Bin"),
                                         &menu);
    recycleBinAct->setToolTip(tr("Open the recycle bin of this note"));
    connect(recycleBinAct, &QAction::triggered,
            this, [this]() {
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                VFile *file = editor->getFile();
                Q_ASSERT(file);

                QString folderPath;
                if (file->getType() == FileType::Note) {
                    const VNoteFile *tmpFile = dynamic_cast<const VNoteFile *>((VFile *)file);
                    folderPath = tmpFile->getNotebook()->getRecycleBinFolderPath();
                } else if (file->getType() == FileType::Orphan) {
                    const VOrphanFile *tmpFile = dynamic_cast<const VOrphanFile *>((VFile *)file);
                    folderPath = tmpFile->fetchRecycleBinFolderPath();
                } else {
                    Q_ASSERT(false);
                }

                QUrl url = QUrl::fromLocalFile(folderPath);
                QDesktopServices::openUrl(url);
            });

    QAction *openLocationAct = new QAction(VIconUtils::menuIcon(":/resources/icons/open_location.svg"),
                                           tr("Open Note Location"),
                                           &menu);
    openLocationAct->setToolTip(tr("Explore the folder containing this note in operating system"));
    connect(openLocationAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                QPointer<VFile> file = editor->getFile();
                Q_ASSERT(file);
                QUrl url = QUrl::fromLocalFile(file->fetchBasePath());
                QDesktopServices::openUrl(url);
            });

    QAction *reloadAct = new QAction(tr("Reload From Disk"), &menu);
    reloadAct->setToolTip(tr("Reload the content of this note from disk"));
    connect(reloadAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                editor->reloadFromDisk();
            });

    QAction *addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                        tr("Add To Cart"),
                                        &menu);
    addToCartAct->setToolTip(tr("Add this note to Cart for further processing"));
    connect(addToCartAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                QPointer<VFile> file = editor->getFile();
                Q_ASSERT(file);
                g_mainWin->getCart()->addFile(file->fetchPath());
                g_mainWin->showStatusMessage(tr("1 note added to Cart"));
            });

    QAction *pinToHistoryAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                           tr("Pin To History"),
                                           &menu);
    pinToHistoryAct->setToolTip(tr("Pin this note to History"));
    connect(pinToHistoryAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                QPointer<VFile> file = editor->getFile();
                Q_ASSERT(file);
                QStringList files;
                files << file->fetchPath();
                g_mainWin->getHistoryList()->pinFiles(files);
                g_mainWin->showStatusMessage(tr("1 note pinned to History"));
            });

    QAction *noteInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/note_info.svg"),
                                       tr("Note Info"),
                                       &menu);
    noteInfoAct->setToolTip(tr("View and edit information of the note"));
    connect(noteInfoAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);

                VEditTab *editor = getTab(tab);
                QPointer<VFile> file = editor->getFile();
                Q_ASSERT(file);
                if (file->getType() == FileType::Note) {
                    VNoteFile *tmpFile = dynamic_cast<VNoteFile *>((VFile *)file);
                    g_mainWin->getFileList()->fileInfo(tmpFile);
                } else if (file->getType() == FileType::Orphan) {
                    g_mainWin->editOrphanFileInfo(file);
                }
            });

    VEditTab *editor = getTab(tab);
    VFile *file = editor->getFile();
    if (file->getType() == FileType::Note) {
        // Locate to folder.
        QAction *locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                                         tr("Locate To Folder"),
                                         &menu);
        locateAct->setToolTip(tr("Locate the folder of current note"));
        VUtils::fixTextWithCaptainShortcut(locateAct, "LocateCurrentFile");
        connect(locateAct, &QAction::triggered,
                this, &VEditWindow::handleLocateAct);
        locateAct->setData(tab);
        menu.addAction(locateAct);

        menu.addSeparator();

        recycleBinAct->setData(tab);
        menu.addAction(recycleBinAct);

        openLocationAct->setData(tab);
        menu.addAction(openLocationAct);

        reloadAct->setData(tab);
        menu.addAction(reloadAct);

        addToCartAct->setData(tab);
        menu.addAction(addToCartAct);

        pinToHistoryAct->setData(tab);
        menu.addAction(pinToHistoryAct);

        noteInfoAct->setData(tab);
        menu.addAction(noteInfoAct);
    } else if (file->getType() == FileType::Orphan
               && !(dynamic_cast<VOrphanFile *>(file)->isSystemFile())) {
        recycleBinAct->setData(tab);
        menu.addAction(recycleBinAct);

        openLocationAct->setData(tab);
        menu.addAction(openLocationAct);

        reloadAct->setData(tab);
        menu.addAction(reloadAct);

        addToCartAct->setData(tab);
        menu.addAction(addToCartAct);

        pinToHistoryAct->setData(tab);
        menu.addAction(pinToHistoryAct);

        noteInfoAct->setData(tab);
        menu.addAction(noteInfoAct);
    }

    int totalWin = m_editArea->windowCount();
    // When there is only one tab and one split window, there is no need to
    // display these two actions.
    if (totalWin > 1 || count() > 1) {
        menu.addSeparator();

        QAction *moveLeftAct = new QAction(VIconUtils::menuIcon(":/resources/icons/move_tab_left.svg"),
                                           tr("Move One Split Left"),
                                           &menu);
        moveLeftAct->setToolTip(tr("Move current tab to the split on the left"));
        VUtils::fixTextWithCaptainShortcut(moveLeftAct, "MoveTabSplitLeft");
        connect(moveLeftAct, &QAction::triggered,
                this, &VEditWindow::handleMoveLeftAct);
        moveLeftAct->setData(tab);
        menu.addAction(moveLeftAct);

        QAction *moveRightAct = new QAction(VIconUtils::menuIcon(":/resources/icons/move_tab_right.svg"),
                                            tr("Move One Split Right"),
                                            &menu);
        moveRightAct->setToolTip(tr("Move current tab to the split on the right"));
        VUtils::fixTextWithCaptainShortcut(moveRightAct, "MoveTabSplitRight");
        connect(moveRightAct, &QAction::triggered,
                this, &VEditWindow::handleMoveRightAct);
        moveRightAct->setData(tab);
        menu.addAction(moveRightAct);
    }

    menu.addSeparator();

    // Close tab, or other tabs, or tabs to the right.
    QAction *closeTabAct = new QAction(VIconUtils::menuIcon(":/resources/icons/close.svg"),
                                       tr("Close Tab"),
                                       &menu);
    closeTabAct->setToolTip(tr("Close current note tab"));
    if (!VUtils::fixTextWithShortcut(closeTabAct, "CloseNote")) {
        VUtils::fixTextWithCaptainShortcut(closeTabAct, "CloseNote");
    }

    connect(closeTabAct, &QAction::triggered,
            this, [this](){
                int tab = GET_TAB_FROM_SENDER();
                Q_ASSERT(tab != -1);
                closeTab(tab);
            });
    closeTabAct->setData(tab);
    menu.addAction(closeTabAct);

    if (count() > 1) {
        QAction *closeOthersAct = new QAction(tr("Close Other Tabs"), &menu);
        closeOthersAct->setToolTip(tr("Close all other note tabs"));
        connect(closeOthersAct, &QAction::triggered,
                this, [this](){
                    int tab = GET_TAB_FROM_SENDER();
                    Q_ASSERT(tab != -1);
                    for (int i = tab - 1; i >= 0; --i) {
                        this->setCurrentIndex(i);
                        this->updateTabStatus(i);
                        if (this->closeTab(i)) {
                            --tab;
                        }
                    }

                    for (int i = tab + 1; i < this->count();) {
                        this->setCurrentIndex(i);
                        this->updateTabStatus(i);
                        if (!this->closeTab(i)) {
                            ++i;
                        }
                    }
                });
        closeOthersAct->setData(tab);
        menu.addAction(closeOthersAct);

        if (tab < count() - 1) {
            QAction *closeRightAct = new QAction(tr("Close Tabs To The Right"), &menu);
            closeRightAct->setToolTip(tr("Close all the note tabs to the right of current tab"));
            connect(closeRightAct, &QAction::triggered,
                    this, [this](){
                        int tab = GET_TAB_FROM_SENDER();
                        Q_ASSERT(tab != -1);
                        for (int i = tab + 1; i < this->count();) {
                            this->setCurrentIndex(i);
                            this->updateTabStatus(i);
                            if (!this->closeTab(i)) {
                                ++i;
                            }
                        }
                    });
            closeRightAct->setData(tab);
            menu.addAction(closeRightAct);
        }
    }

    QAction *closeAllAct = new QAction(tr("Close All Tabs"), &menu);
    closeAllAct->setToolTip(tr("Close all the note tabs"));
    connect(closeAllAct, &QAction::triggered,
            this, [this](){
                for (int i = 0; i < this->count();) {
                    this->setCurrentIndex(i);
                    this->updateTabStatus(i);
                    if (!this->closeTab(i)) {
                        ++i;
                    }
                }
            });
    menu.addAction(closeAllAct);

    if (!menu.actions().isEmpty()) {
        menu.exec(bar->mapToGlobal(p_pos));
    }
}

void VEditWindow::tabListJump(VFile *p_file)
{
    if (!p_file) {
        return;
    }

    int idx = findTabByFile(p_file);
    Q_ASSERT(idx >= 0);
    setCurrentIndex(idx);
    focusWindow();
    updateTabStatus(idx);
}

void VEditWindow::updateSplitMenu(QMenu *p_menu)
{
    if (p_menu->isEmpty()) {
        QAction *splitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/split_window.svg"),
                                        tr("Split"),
                                        p_menu);
        splitAct->setToolTip(tr("Split current window vertically"));
        VUtils::fixTextWithCaptainShortcut(splitAct, "VerticalSplit");
        connect(splitAct, &QAction::triggered,
                this, [this](){
                    splitWindow(true);
                });

        p_menu->addAction(splitAct);

        p_menu->addAction(getRemoveSplitAction());
    }

    getRemoveSplitAction()->setVisible(canRemoveSplit());
}

bool VEditWindow::canRemoveSplit()
{
    QSplitter *splitter = dynamic_cast<QSplitter *>(parent());
    Q_ASSERT(splitter);
    return splitter->count() > 1;
}

void VEditWindow::handleTabOutlineChanged(const VTableOfContent &p_outline)
{
    // Only propagate it if it is current tab.
    VEditTab *tab = getCurrentTab();
    if (tab) {
        if (tab->getFile() == p_outline.getFile()) {
            emit outlineChanged(p_outline);
        }
    } else {
        emit outlineChanged(VTableOfContent());
        return;
    }
}

void VEditWindow::handleTabCurrentHeaderChanged(const VHeaderPointer &p_header)
{
    // Only propagate it if it is current tab.
    VEditTab *tab = getCurrentTab();
    if (tab) {
        if (tab->getFile() == p_header.m_file) {
            emit currentHeaderChanged(p_header);
        }
    } else {
        emit currentHeaderChanged(VHeaderPointer());
        return;
    }
}

void VEditWindow::scrollToHeader(const VHeaderPointer &p_header)
{
    VEditTab *tab = getCurrentTab();
    if (tab) {
        tab->scrollToHeader(p_header);
    }
}

void VEditWindow::handleTabStatusUpdated(const VEditTabInfo &p_info)
{
    int idx = indexOf(dynamic_cast<QWidget *>(sender()));

    if (p_info.m_type == VEditTabInfo::InfoType::All) {
        updateTabInfo(idx);
        updateAllTabsSequence();
    }

    if (idx == currentIndex()) {
        // Current tab. Propogate its status.
        emit tabStatusUpdated(p_info);
    }
}

void VEditWindow::handleTabStatusMessage(const QString &p_msg)
{
    int idx = indexOf(dynamic_cast<QWidget *>(sender()));
    if (idx == currentIndex()) {
        emit statusMessage(p_msg);
    }
}

void VEditWindow::handleTabVimStatusUpdated(const VVim *p_vim)
{
    int idx = indexOf(dynamic_cast<QWidget *>(sender()));
    if (idx == currentIndex()) {
        emit vimStatusUpdated(p_vim);
    }
}

void VEditWindow::updateFileInfo(const VFile *p_file, UpdateAction p_act)
{
    if (!p_file) {
        return;
    }

    int idx = findTabByFile(p_file);
    if (idx > -1) {
        updateTabStatus(idx);
        getTab(idx)->handleFileOrDirectoryChange(true, p_act);
    }
}

void VEditWindow::updateDirectoryInfo(const VDirectory *p_dir, UpdateAction p_act)
{
    if (!p_dir) {
        return;
    }

    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        VEditTab *editor = getTab(i);
        QPointer<VFile> file = editor->getFile();
        if (p_dir->containsFile(file)) {
            updateTabStatus(i);
            editor->handleFileOrDirectoryChange(false, p_act);
        }
    }
}

void VEditWindow::updateNotebookInfo(const VNotebook *p_notebook)
{
    if (!p_notebook) {
        return;
    }

    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        VEditTab *editor = getTab(i);
        QPointer<VFile> file = editor->getFile();
        if (p_notebook->containsFile(file)) {
            updateTabStatus(i);
        }
    }
}

VEditTab *VEditWindow::getCurrentTab() const
{
    int idx = currentIndex();
    if (idx == -1) {
        return NULL;
    }

    return getTab(idx);
}

QVector<VEditTabInfo> VEditWindow::getAllTabsInfo() const
{
    int nrTab = count();

    QVector<VEditTabInfo> tabs;
    tabs.reserve(nrTab);
    for (int i = 0; i < nrTab; ++i) {
        VEditTab *editTab = getTab(i);
        tabs.push_back(editTab->fetchTabInfo());
    }

    return tabs;
}

void VEditWindow::handleLocateAct()
{
    int tab = GET_TAB_FROM_SENDER();
    VEditTab *editor = getTab(tab);
    QPointer<VFile> file = editor->getFile();
    if (file->getType() == FileType::Note) {
        g_mainWin->locateFile(file);
    }
}

void VEditWindow::handleMoveLeftAct()
{
    int tab = GET_TAB_FROM_SENDER();
    Q_ASSERT(tab != -1);
    moveTabOneSplit(tab, false);
}

void VEditWindow::handleMoveRightAct()
{
    int tab = GET_TAB_FROM_SENDER();
    Q_ASSERT(tab != -1);
    moveTabOneSplit(tab, true);
}

void VEditWindow::moveTabOneSplit(int p_tabIdx, bool p_right)
{
    Q_ASSERT(p_tabIdx > -1 && p_tabIdx < count());
    // Add split window if needed.
    if (m_editArea->windowCount() < 2) {
        // Request VEditArea to split window.
        splitWindow(p_right);

        // Though the signal and slot will behave like a function call. We wait
        // here until the window split finished.
        while (m_editArea->windowCount() < 2) {
            VUtils::sleepWait(100);
        }
    }

    int totalWin = m_editArea->windowCount();
    int idx = m_editArea->windowIndex(this);
    int newIdx = p_right ? idx + 1 : idx - 1;
    if (newIdx >= totalWin) {
        newIdx = 0;
    } else if (newIdx < 0) {
        newIdx = totalWin - 1;
    }
    VEditTab *editor = getTab(p_tabIdx);
    // Remove it from current window. This won't close the split even if it is
    // the only tab.
    removeTab(p_tabIdx);

    // Disconnect all the signals.
    disconnect(editor, 0, this, 0);

    m_editArea->moveTab(editor, idx, newIdx);

    // If there is no tab, remove current split.
    if (count() == 0) {
        emit requestRemoveSplit(this);
    }
}

void VEditWindow::moveCurrentTabOneSplit(bool p_right)
{
    int idx = currentIndex();
    if (idx == -1) {
        return;
    }

    moveTabOneSplit(idx, p_right);
}

bool VEditWindow::addEditTab(QWidget *p_widget)
{
    if (!p_widget) {
        return false;
    }

    VEditTab *editor = dynamic_cast<VEditTab *>(p_widget);
    if (!editor) {
        return false;
    }

    // Connect the signals.
    connectEditTab(editor);

    int idx = insertEditTab(currentIndex() + 1, editor->getFile(), editor);
    setCurrentIndex(idx);
    updateTabStatus(idx);
    return true;
}

void VEditWindow::connectEditTab(const VEditTab *p_tab)
{
    connect(p_tab, &VEditTab::getFocused,
            this, [this]() {
                setCurrentWidget(static_cast<VEditTab *>(sender()));
                emit getFocused();
            });
    connect(p_tab, &VEditTab::outlineChanged,
            this, &VEditWindow::handleTabOutlineChanged);
    connect(p_tab, &VEditTab::currentHeaderChanged,
            this, &VEditWindow::handleTabCurrentHeaderChanged);
    connect(p_tab, &VEditTab::statusUpdated,
            this, &VEditWindow::handleTabStatusUpdated);
    connect(p_tab, &VEditTab::statusMessage,
            this, &VEditWindow::handleTabStatusMessage);
    connect(p_tab, &VEditTab::vimStatusUpdated,
            this, &VEditWindow::handleTabVimStatusUpdated);
    connect(p_tab, &VEditTab::closeRequested,
            this, &VEditWindow::tabRequestToClose);
}

void VEditWindow::setCurrentWindow(bool p_current)
{
    static QIcon menuCur = VIconUtils::editWindowCornerIcon(":/resources/icons/corner_menu_cur.svg");
    static QIcon tablistCur = VIconUtils::editWindowCornerIcon(":/resources/icons/corner_tablist_cur.svg");
    static QIcon menu = VIconUtils::editWindowCornerInactiveIcon(":/resources/icons/corner_menu.svg");
    static QIcon tablist = VIconUtils::editWindowCornerInactiveIcon(":/resources/icons/corner_tablist.svg");

    if (p_current) {
        rightBtn->setIcon(menuCur);
        leftBtn->setIcon(tablistCur);
    } else {
        rightBtn->setIcon(menu);
        leftBtn->setIcon(tablist);
    }
}

void VEditWindow::clearSearchedWordHighlight()
{
    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        getTab(i)->clearSearchedWordHighlight();
    }
}

void VEditWindow::focusNextTab(bool p_right)
{
    focusWindow();
    if (count() < 2) {
        return;
    }
    int idx = currentIndex();
    idx = p_right ? idx + 1 : idx - 1;
    if (idx < 0) {
        idx = count() - 1;
    } else if (idx >= count()) {
        idx = 0;
    }
    setCurrentIndex(idx);
}

bool VEditWindow::showOpenedFileList()
{
    if (count() == 0) {
        return false;
    }

    leftBtn->showMenu();
    VOpenedListMenu *menu = dynamic_cast<VOpenedListMenu *>(leftBtn->menu());
    return menu->isAccepted();
}

bool VEditWindow::activateTab(int p_sequence)
{
    if (p_sequence < c_tabSequenceBase
        || p_sequence >= (c_tabSequenceBase + count())) {
        return false;
    }

    int idx = p_sequence - c_tabSequenceBase;
    setCurrentIndex(idx);
    // Always need to focus to this tab to meet the requirement of Captain mode.
    getTab(idx)->focusTab();

    return true;
}

bool VEditWindow::alternateTab()
{
    if (m_lastTabWidget) {
        if (-1 != indexOf(m_lastTabWidget)) {
            setCurrentWidget(m_lastTabWidget);
            return true;
        } else {
            m_lastTabWidget = NULL;
        }
    }
    return false;
}

VEditTab* VEditWindow::getTab(int tabIndex) const
{
    return dynamic_cast<VEditTab *>(widget(tabIndex));
}

void VEditWindow::dragEnterEvent(QDragEnterEvent *p_event)
{
    if (p_event->mimeData()->hasFormat("text/uri-list")) {
        p_event->acceptProposedAction();
        return;
    }

    QTabWidget::dragEnterEvent(p_event);
}

void VEditWindow::dropEvent(QDropEvent *p_event)
{
    const QMimeData *mime = p_event->mimeData();
    if (mime->hasFormat("text/uri-list") && mime->hasUrls()) {
        // Open external files in this edit window.
        QStringList files;
        QList<QUrl> urls = mime->urls();
        for (int i = 0; i < urls.size(); ++i) {
            QString file;
            if (urls[i].isLocalFile()) {
                file = urls[i].toLocalFile();
                QFileInfo fi(file);
                if (fi.exists() && fi.isFile()) {
                    file = QDir::cleanPath(fi.absoluteFilePath());
                    files.append(file);
                }
            }
        }

        if (!files.isEmpty()) {
            focusWindow();
            g_mainWin->openFiles(files);
        }

        p_event->acceptProposedAction();
        return;
    }

    QTabWidget::dropEvent(p_event);
}

QVector<VEditTab *> VEditWindow::getAllTabs() const
{
    int nrTab = count();

    QVector<VEditTab *> tabs;
    tabs.reserve(nrTab);
    for (int i = 0; i < nrTab; ++i) {
        tabs.push_back(getTab(i));
    }

    return tabs;
}

void VEditWindow::checkFileChangeOutside()
{
    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        getTab(i)->checkFileChangeOutside();
    }
}

void VEditWindow::saveAll()
{
    int nrTab = count();
    for (int i = 0; i < nrTab; ++i) {
        getTab(i)->saveFile();
    }
}

void VEditWindow::tabRequestToClose(VEditTab *p_tab)
{
    bool ok = p_tab->closeFile(false);
    if (ok) {
        removeTab(indexOf(p_tab));

        // Disconnect all the signals.
        disconnect(p_tab, 0, this, 0);

        p_tab->deleteLater();
    }
}

int VEditWindow::tabBarHeight() const
{
    return tabBar()->height();
}

QVector<TabNavigationInfo> VEditWindow::getTabsNavigationInfo() const
{
    QVector<TabNavigationInfo> infos;
    QTabBar *bar = tabBar();
    for (int i = 0; i < bar->count(); ++i) {
        QPoint tl = bar->tabRect(i).topLeft();
        if (tl.x() < 0 || tl.x() >= bar->width()) {
            continue;
        }

        TabNavigationInfo info;
        info.m_topLeft = bar->mapToParent(tl);
        info.m_tab = getTab(i);
        infos.append(info);
    }

    return infos;
}
