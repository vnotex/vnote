#include <QtWidgets>
#include <QtDebug>
#include "veditwindow.h"
#include "vedittab.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vfile.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vopenedlistmenu.h"
#include "vmdtab.h"
#include "vhtmltab.h"

extern VConfigManager vconfig;

VEditWindow::VEditWindow(VNote *vnote, VEditArea *editArea, QWidget *parent)
    : QTabWidget(parent), vnote(vnote), m_editArea(editArea),
      m_curTabWidget(NULL), m_lastTabWidget(NULL)
{
    initTabActions();
    setupCornerWidget();

    // Explicit speficy in macOS.
    setUsesScrollButtons(true);
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
    connect(this, &VEditWindow::currentChanged,
            this, &VEditWindow::handleCurrentIndexChanged);
    connect(this, &VEditWindow::customContextMenuRequested,
            this, &VEditWindow::contextMenuRequested);
}

void VEditWindow::initTabActions()
{
    m_locateAct = new QAction(QIcon(":/resources/icons/locate_note.svg"),
                              tr("Locate To Folder"), this);
    m_locateAct->setToolTip(tr("Locate the folder of current note"));
    connect(m_locateAct, &QAction::triggered,
            this, &VEditWindow::handleLocateAct);

    m_moveLeftAct = new QAction(QIcon(":/resources/icons/move_tab_left.svg"),
                                tr("Move One Split Left"), this);
    m_moveLeftAct->setToolTip(tr("Move current tab to the split on the left"));
    connect(m_moveLeftAct, &QAction::triggered,
            this, &VEditWindow::handleMoveLeftAct);

    m_moveRightAct = new QAction(QIcon(":/resources/icons/move_tab_right.svg"),
                                 tr("Move One Split Right"), this);
    m_moveRightAct->setToolTip(tr("Move current tab to the split on the right"));
    connect(m_moveRightAct, &QAction::triggered,
            this, &VEditWindow::handleMoveRightAct);

    m_closeTabAct = new QAction(tr("Close Tab"), this);
    m_closeTabAct->setToolTip(tr("Close current note tab"));
    connect(m_closeTabAct, &QAction::triggered,
            this, [this](){
                int tab = this->m_closeTabAct->data().toInt();
                Q_ASSERT(tab != -1);
                closeTab(tab);
            });

    m_closeOthersAct = new QAction(tr("Close Other Tabs"), this);
    m_closeOthersAct->setToolTip(tr("Close all other note tabs"));
    connect(m_closeOthersAct, &QAction::triggered,
            this, [this](){
                int tab = this->m_closeTabAct->data().toInt();
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

    m_closeRightAct = new QAction(tr("Close Tabs To The Right"), this);
    m_closeRightAct->setToolTip(tr("Close all the note tabs to the right of current tab"));
    connect(m_closeRightAct, &QAction::triggered,
            this, [this](){
                int tab = this->m_closeTabAct->data().toInt();
                Q_ASSERT(tab != -1);

                for (int i = tab + 1; i < this->count();) {
                    this->setCurrentIndex(i);
                    this->updateTabStatus(i);
                    if (!this->closeTab(i)) {
                        ++i;
                    }
                }
            });
}

void VEditWindow::setupCornerWidget()
{
    // Left corner button
    leftBtn = new QPushButton(QIcon(":/resources/icons/corner_tablist.svg"),
                              "", this);
    leftBtn->setProperty("CornerBtn", true);
    VOpenedListMenu *leftMenu = new VOpenedListMenu(this);
    leftMenu->setToolTipsVisible(true);
    connect(leftMenu, &VOpenedListMenu::fileTriggered,
            this, &VEditWindow::tabListJump);
    leftBtn->setMenu(leftMenu);
    setCornerWidget(leftBtn, Qt::TopLeftCorner);

    // Right corner button
    // Actions
    splitAct = new QAction(QIcon(":/resources/icons/split_window.svg"),
                           tr("Split"), this);
    splitAct->setToolTip(tr("Split current window vertically"));
    connect(splitAct, &QAction::triggered,
            this, [this](){
                splitWindow(true);
            });

    removeSplitAct = new QAction(QIcon(":/resources/icons/remove_split.svg"),
                                 tr("Remove split"), this);
    removeSplitAct->setToolTip(tr("Remove current split window"));
    connect(removeSplitAct, &QAction::triggered,
            this, &VEditWindow::removeSplit);

    rightBtn = new QPushButton(QIcon(":/resources/icons/corner_menu.svg"),
                               "", this);
    rightBtn->setProperty("CornerBtn", true);
    QMenu *rightMenu = new QMenu(this);
    rightMenu->setToolTipsVisible(true);
    rightMenu->addAction(splitAct);
    rightMenu->addAction(removeSplitAct);
    rightBtn->setMenu(rightMenu);
    setCornerWidget(rightBtn, Qt::TopRightCorner);
    connect(rightMenu, &QMenu::aboutToShow,
            this, &VEditWindow::updateSplitMenu);
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
    int idx = insertTab(p_index, p_page, p_file->getName());
    setTabToolTip(idx, generateTooltip(p_file));
    return idx;
}

int VEditWindow::appendEditTab(VFile *p_file, QWidget *p_page)
{
    return insertEditTab(count(), p_file, p_page);
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
    if (!p_forced) {
        setCurrentIndex(idx);
        updateTabStatus(idx);
    }
    // Even p_forced is true we need to delete unused images.
    bool ok = editor->closeFile(p_forced);
    if (ok) {
        removeEditTab(idx);
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

        if (!p_forced) {
            setCurrentIndex(0);
            updateTabStatus(0);
        }
        // Even p_forced is true we need to delete unused images.
        bool ok = editor->closeFile(p_forced);
        if (ok) {
            removeEditTab(0);
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

    int idx = appendEditTab(p_file, editor);
    return idx;
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
    bool ok = editor->closeFile(false);
    if (ok) {
        removeEditTab(p_index);
    }

    // User clicks the close button. We should make this window
    // to be current window.
    emit getFocused();
    if (count() == 0) {
        emit requestRemoveSplit(this);
    }
    return ok;
}

void VEditWindow::readFile()
{
    VEditTab *editor = getTab(currentIndex());
    Q_ASSERT(editor);
    editor->readFile();
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
        emit outlineChanged(VToc());
        emit curHeaderChanged(VAnchor());
        return;
    }

    VEditTab *tab = getTab(p_index);
    tab->updateStatus();
    tab->requestUpdateOutline();
    tab->requestUpdateCurHeader();
}

void VEditWindow::updateTabInfo(int p_index)
{
    VEditTab *editor = getTab(p_index);
    const VFile *file = editor->getFile();
    bool editMode = editor->isEditMode();

    setTabText(p_index, generateTabText(p_index, file->getName(),
                                        file->isModified(), file->isModifiable()));
    setTabToolTip(p_index, generateTooltip(file));
    setTabIcon(p_index, editMode ? QIcon(":/resources/icons/editing.svg") :
               QIcon(":/resources/icons/reading.svg"));
}

void VEditWindow::updateAllTabsSequence()
{
    for (int i = 0; i < count(); ++i) {
        VEditTab *editor = getTab(i);
        const VFile *file = editor->getFile();
        setTabText(i, generateTabText(i, file->getName(),
                                      file->isModified(), file->isModifiable()));
    }
}

// Be requested to report current outline
void VEditWindow::requestUpdateOutline()
{
    int idx = currentIndex();
    if (idx == -1) {
        emit outlineChanged(VToc());
        return;
    }
    getTab(idx)->requestUpdateOutline();
}

// Be requested to report current header
void VEditWindow::requestUpdateCurHeader()
{
    int idx = currentIndex();
    if (idx == -1) {
        emit curHeaderChanged(VAnchor());
        return;
    }
    getTab(idx)->requestUpdateCurHeader();
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

void VEditWindow::contextMenuRequested(QPoint pos)
{
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (canRemoveSplit()) {
        menu.addAction(removeSplitAct);
        menu.exec(this->mapToGlobal(pos));
    }
}

void VEditWindow::tabbarContextMenuRequested(QPoint p_pos)
{
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QTabBar *bar = tabBar();
    int tab = bar->tabAt(p_pos);
    if (tab == -1) {
        return;
    }

    m_locateAct->setData(tab);
    VEditTab *editor = getTab(tab);
    QPointer<VFile> file = editor->getFile();
    if (file->getType() == FileType::Normal) {
        // Locate to folder.
        menu.addAction(m_locateAct);
    }

    int totalWin = m_editArea->windowCount();
    // When there is only one tab and one split window, there is no need to
    // display these two actions.
    if (totalWin > 1 || count() > 1) {
        menu.addSeparator();
        m_moveLeftAct->setData(tab);
        // Move one split left.
        menu.addAction(m_moveLeftAct);

        m_moveRightAct->setData(tab);
        // Move one split right.
        menu.addAction(m_moveRightAct);
    }

    // Close tab, or other tabs, or tabs to the right.
    menu.addSeparator();
    m_closeTabAct->setData(tab);
    menu.addAction(m_closeTabAct);
    if (count() > 1) {
        m_closeOthersAct->setData(tab);
        menu.addAction(m_closeOthersAct);
        if (tab < count() - 1) {
            m_closeRightAct->setData(tab);
            menu.addAction(m_closeRightAct);
        }
    }

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
    updateTabStatus(idx);
}

void VEditWindow::updateSplitMenu()
{
    if (canRemoveSplit()) {
        removeSplitAct->setVisible(true);
    } else {
        removeSplitAct->setVisible(false);
    }
}

bool VEditWindow::canRemoveSplit()
{
    QSplitter *splitter = dynamic_cast<QSplitter *>(parent());
    Q_ASSERT(splitter);
    return splitter->count() > 1;
}

void VEditWindow::handleOutlineChanged(const VToc &p_toc)
{
    // Only propagate it if it is current tab
    int idx = currentIndex();
    if (idx == -1) {
        emit outlineChanged(VToc());
        return;
    }
    const VFile *file = getTab(idx)->getFile();
    if (p_toc.m_file == file) {
        emit outlineChanged(p_toc);
    }
}

void VEditWindow::handleCurHeaderChanged(const VAnchor &p_anchor)
{
    // Only propagate it if it is current tab
    int idx = currentIndex();
    if (idx == -1) {
        emit curHeaderChanged(VAnchor());
        return;
    }
    const VFile *file = getTab(idx)->getFile();
    if (p_anchor.m_file == file) {
        emit curHeaderChanged(p_anchor);
    }
}

void VEditWindow::scrollCurTab(const VAnchor &p_anchor)
{
    int idx = currentIndex();
    if (idx == -1) {
        emit curHeaderChanged(VAnchor());
        return;
    }
    const VFile *file = getTab(idx)->getFile();
    if (file == p_anchor.m_file) {
        getTab(idx)->scrollToAnchor(p_anchor);
    }
}

void VEditWindow::handleTabStatusUpdated(const VEditTabInfo &p_info)
{
    int idx = indexOf(dynamic_cast<QWidget *>(sender()));

    updateTabInfo(idx);
    updateAllTabsSequence();

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

void VEditWindow::updateFileInfo(const VFile *p_file)
{
    if (!p_file) {
        return;
    }
    int idx = findTabByFile(p_file);
    if (idx > -1) {
        updateTabStatus(idx);
    }
}

void VEditWindow::updateDirectoryInfo(const VDirectory *p_dir)
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

VEditTab *VEditWindow::currentEditTab()
{
    int idx = currentIndex();
    if (idx == -1) {
        return NULL;
    }
    return getTab(idx);
}

void VEditWindow::handleLocateAct()
{
    int tab = m_locateAct->data().toInt();
    VEditTab *editor = getTab(tab);
    QPointer<VFile> file = editor->getFile();
    if (file->getType() == FileType::Normal) {
        vnote->getMainWindow()->locateFile(file);
    }
}

void VEditWindow::handleMoveLeftAct()
{
    int tab = m_moveLeftAct->data().toInt();
    Q_ASSERT(tab != -1);
    moveTabOneSplit(tab, false);
}

void VEditWindow::handleMoveRightAct()
{
    int tab = m_moveRightAct->data().toInt();
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

    int idx = appendEditTab(editor->getFile(), editor);
    setCurrentIndex(idx);
    updateTabStatus(idx);
    return true;
}

void VEditWindow::connectEditTab(const VEditTab *p_tab)
{
    connect(p_tab, &VEditTab::getFocused,
            this, &VEditWindow::getFocused);
    connect(p_tab, &VEditTab::outlineChanged,
            this, &VEditWindow::handleOutlineChanged);
    connect(p_tab, &VEditTab::curHeaderChanged,
            this, &VEditWindow::handleCurHeaderChanged);
    connect(p_tab, &VEditTab::statusUpdated,
            this, &VEditWindow::handleTabStatusUpdated);
    connect(p_tab, &VEditTab::statusMessage,
            this, &VEditWindow::handleTabStatusMessage);
    connect(p_tab, &VEditTab::vimStatusUpdated,
            this, &VEditWindow::handleTabVimStatusUpdated);
}

void VEditWindow::setCurrentWindow(bool p_current)
{
    if (p_current) {
        rightBtn->setIcon(QIcon(":/resources/icons/corner_menu_cur.svg"));
        leftBtn->setIcon(QIcon(":/resources/icons/corner_tablist_cur.svg"));
    } else {
        rightBtn->setIcon(QIcon(":/resources/icons/corner_menu.svg"));
        leftBtn->setIcon(QIcon(":/resources/icons/corner_tablist.svg"));
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
    return true;
}

bool VEditWindow::activateTab(int p_sequence)
{
    if (p_sequence < c_tabSequenceBase || p_sequence >= (c_tabSequenceBase + count())) {
        return false;
    }
    setCurrentIndex(p_sequence - c_tabSequenceBase);
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

