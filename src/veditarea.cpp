#include <QtWidgets>
#include "veditarea.h"
#include "veditwindow.h"
#include "vedittab.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vfile.h"
#include "dialog/vfindreplacedialog.h"
#include "utils/vutils.h"
#include "vfilesessioninfo.h"
#include "vmainwindow.h"
#include "vcaptain.h"
#include "vfilelist.h"

extern VConfigManager *g_config;

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

VEditArea::VEditArea(QWidget *parent)
    : QWidget(parent),
      VNavigationMode(),
      curWindowIndex(-1)
{
    setupUI();

    insertSplitWindow(0);
    setCurrentWindow(0, false);

    registerCaptainTargets();


    QString keySeq = g_config->getShortcutKeySequence("ActivateNextTab");
    qDebug() << "set ActivateNextTab shortcut to" << keySeq;
    QShortcut *activateNextTab = new QShortcut(QKeySequence(keySeq), this);
    activateNextTab->setContext(Qt::ApplicationShortcut);
    connect(activateNextTab, &QShortcut::activated,
            this, [this]() {
                VEditWindow *win = getCurrentWindow();
                if (win) {
                    win->focusNextTab(true);
                }
            });

    keySeq = g_config->getShortcutKeySequence("ActivatePreviousTab");
    qDebug() << "set ActivatePreviousTab shortcut to" << keySeq;
    QShortcut *activatePreviousTab = new QShortcut(QKeySequence(keySeq), this);
    activatePreviousTab->setContext(Qt::ApplicationShortcut);
    connect(activatePreviousTab, &QShortcut::activated,
            this, [this]() {
                VEditWindow *win = getCurrentWindow();
                if (win) {
                    win->focusNextTab(false);
                }
            });

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(false);
    timer->setInterval(g_config->getFileTimerInterval());
    connect(timer, &QTimer::timeout,
            this, &VEditArea::handleFileTimerTimeout);

    timer->start();

    m_autoSave = g_config->getEnableAutoSave();
}

void VEditArea::setupUI()
{
    splitter = new QSplitter(this);
    m_findReplace = new VFindReplaceDialog(this);
    m_findReplace->setOption(FindOption::CaseSensitive,
                             g_config->getFindCaseSensitive());
    m_findReplace->setOption(FindOption::WholeWordOnly,
                             g_config->getFindWholeWordOnly());
    m_findReplace->setOption(FindOption::RegularExpression,
                             g_config->getFindRegularExpression());
    m_findReplace->setOption(FindOption::IncrementalSearch,
                             g_config->getFindIncrementalSearch());

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(splitter);
    mainLayout->addWidget(m_findReplace);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->setStretch(0, 1);
    mainLayout->setStretch(1, 0);

    setLayout(mainLayout);

    connect(m_findReplace, &VFindReplaceDialog::findTextChanged,
            this, &VEditArea::handleFindTextChanged);
    connect(m_findReplace, &VFindReplaceDialog::findOptionChanged,
            this, &VEditArea::handleFindOptionChanged);
    connect(m_findReplace, SIGNAL(findNext(const QString &, uint, bool)),
            this, SLOT(handleFindNext(const QString &, uint, bool)));
    connect(m_findReplace,
            SIGNAL(replace(const QString &, uint, const QString &, bool)),
            this,
            SLOT(handleReplace(const QString &, uint, const QString &, bool)));
    connect(m_findReplace,
            SIGNAL(replaceAll(const QString &, uint, const QString &)),
            this,
            SLOT(handleReplaceAll(const QString &, uint, const QString &)));
    connect(m_findReplace, &VFindReplaceDialog::dialogClosed,
            this, &VEditArea::handleFindDialogClosed);
    m_findReplace->hide();

    // Shortcut Ctrl+Shift+T to open last closed file.
    QString keySeq = g_config->getShortcutKeySequence("LastClosedFile");
    qDebug() << "set LastClosedFile shortcut to" << keySeq;
    QShortcut *lastClosedFileShortcut = new QShortcut(QKeySequence(keySeq), this);
    lastClosedFileShortcut->setContext(Qt::ApplicationShortcut);
    connect(lastClosedFileShortcut, &QShortcut::activated,
            this, [this]() {
                if (!m_lastClosedFiles.isEmpty()) {
                    const VFileSessionInfo &file = m_lastClosedFiles.top();
                    QVector<VFileSessionInfo> files(1, file);
                    m_lastClosedFiles.pop();

                    openFiles(files);
                }
            });
}

void VEditArea::insertSplitWindow(int idx)
{
    VEditWindow *win = new VEditWindow(this);
    splitter->insertWidget(idx, win);
    connect(win, &VEditWindow::tabStatusUpdated,
            this, &VEditArea::handleWindowTabStatusUpdated);
    connect(win, &VEditWindow::requestSplitWindow,
            this, &VEditArea::splitWindow);
    connect(win, &VEditWindow::requestRemoveSplit,
            this, &VEditArea::handleRemoveSplitRequest);
    connect(win, &VEditWindow::getFocused,
            this, &VEditArea::handleWindowFocused);
    connect(win, &VEditWindow::outlineChanged,
            this, &VEditArea::handleWindowOutlineChanged);
    connect(win, &VEditWindow::currentHeaderChanged,
            this, &VEditArea::handleWindowCurrentHeaderChanged);
    connect(win, &VEditWindow::statusMessage,
            this, &VEditArea::handleWindowStatusMessage);
    connect(win, &VEditWindow::vimStatusUpdated,
            this, &VEditArea::handleWindowVimStatusUpdated);
}

void VEditArea::handleWindowTabStatusUpdated(const VEditTabInfo &p_info)
{
    if (splitter->widget(curWindowIndex) == sender()) {
        emit tabStatusUpdated(p_info);
    }
}

void VEditArea::handleWindowStatusMessage(const QString &p_msg)
{
    if (splitter->widget(curWindowIndex) == sender()) {
        emit statusMessage(p_msg);
    }
}

void VEditArea::handleWindowVimStatusUpdated(const VVim *p_vim)
{
    if (splitter->widget(curWindowIndex) == sender()) {
        emit vimStatusUpdated(p_vim);
    }
}

void VEditArea::removeSplitWindow(VEditWindow *win)
{
    if (!win) {
        return;
    }

    win->hide();
    win->setParent(this);
    disconnect(win, 0, this, 0);
    // Should be deleted later
    win->deleteLater();
}

VEditTab *VEditArea::openFile(VFile *p_file, OpenFileMode p_mode, bool p_forceMode)
{
    if (!p_file) {
        return NULL;
    }

    // Update auto save settings.
    m_autoSave = g_config->getEnableAutoSave();

    // If it is DocType::Unknown, open it using system default method.
    if (p_file->getDocType() == DocType::Unknown) {
        QUrl url = QUrl::fromLocalFile(p_file->fetchPath());
        QDesktopServices::openUrl(url);
        return NULL;
    }

    // Find if it has been opened already
    int winIdx, tabIdx;
    bool existFile = false;
    bool setFocus = false;
    auto tabs = findTabsByFile(p_file);
    if (!tabs.empty()) {
        // Current window first
        winIdx = tabs[0].first;
        tabIdx = tabs[0].second;
        for (int i = 0; i < tabs.size(); ++i) {
            if (tabs[i].first == curWindowIndex) {
                winIdx = tabs[i].first;
                tabIdx = tabs[i].second;
                break;
            }
        }

        setFocus = true;
        existFile = true;
        goto out;
    }

    // Open it in current window
    if (curWindowIndex == -1) {
        // insert a new split
        insertSplitWindow(0);
        curWindowIndex = 0;
    }

    winIdx = curWindowIndex;
    tabIdx = openFileInWindow(winIdx, p_file, p_mode);

out:
    VEditTab *tab = getTab(winIdx, tabIdx);

    setCurrentTab(winIdx, tabIdx, setFocus);

    if (existFile && p_forceMode) {
        if (p_mode == OpenFileMode::Read) {
            readFile();
        } else if (p_mode == OpenFileMode::Edit) {
            editFile();
        }
    }

    return tab;
}

QVector<QPair<int, int> > VEditArea::findTabsByFile(const VFile *p_file)
{
    QVector<QPair<int, int> > tabs;
    int nrWin = splitter->count();
    for (int winIdx = 0; winIdx < nrWin; ++winIdx) {
        VEditWindow *win = getWindow(winIdx);
        int tabIdx = win->findTabByFile(p_file);
        if (tabIdx != -1) {
            QPair<int, int> match;
            match.first = winIdx;
            match.second = tabIdx;
            tabs.append(match);
        }
    }
    return tabs;
}

int VEditArea::openFileInWindow(int windowIndex, VFile *p_file, OpenFileMode p_mode)
{
    Q_ASSERT(windowIndex < splitter->count());
    VEditWindow *win = getWindow(windowIndex);
    return win->openFile(p_file, p_mode);
}

void VEditArea::setCurrentTab(int windowIndex, int tabIndex, bool setFocus)
{
    VEditWindow *win = getWindow(windowIndex);
    win->setCurrentIndex(tabIndex);

    setCurrentWindow(windowIndex, setFocus);
}

void VEditArea::setCurrentWindow(int windowIndex, bool setFocus)
{
    int nrWin = splitter->count();
    curWindowIndex = windowIndex;

    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->setCurrentWindow(false);
    }
    if (curWindowIndex > -1) {
        getWindow(curWindowIndex)->setCurrentWindow(true);
        if (setFocus) {
            getWindow(curWindowIndex)->focusWindow();
        }
    }
    // Update status
    updateWindowStatus();
}

void VEditArea::updateWindowStatus()
{
    if (curWindowIndex == -1) {
        Q_ASSERT(splitter->count() == 0);

        emit tabStatusUpdated(VEditTabInfo());
        emit outlineChanged(VTableOfContent());
        emit currentHeaderChanged(VHeaderPointer());
        return;
    }

    VEditWindow *win = getWindow(curWindowIndex);
    win->updateTabStatus();
}

bool VEditArea::closeFile(const VFile *p_file, bool p_forced)
{
    if (!p_file) {
        return true;
    }
    bool ret = false;
    int i = 0;
    while (i < splitter->count()) {
        VEditWindow *win = getWindow(i);
        int nrWin = splitter->count();
        ret = ret || win->closeFile(p_file, p_forced);
        if (nrWin == splitter->count()) {
            ++i;
        }
    }
    updateWindowStatus();
    return ret;
}

bool VEditArea::closeFile(const VDirectory *p_dir, bool p_forced)
{
    if (!p_dir) {
        return true;
    }
    int i = 0;
    while (i < splitter->count()) {
        VEditWindow *win = getWindow(i);
        if (!p_forced) {
            setCurrentWindow(i, false);
        }
        int nrWin = splitter->count();
        if (!win->closeFile(p_dir, p_forced)) {
            return false;
        }
        // win may be removed after closeFile()
        if (nrWin == splitter->count()) {
            ++i;
        }
    }
    updateWindowStatus();
    return true;
}

bool VEditArea::closeFile(const VNotebook *p_notebook, bool p_forced)
{
    if (!p_notebook) {
        return true;
    }
    int i = 0;
    while (i < splitter->count()) {
        VEditWindow *win = getWindow(i);
        if (!p_forced) {
            setCurrentWindow(i, false);
        }
        int nrWin = splitter->count();
        if (!win->closeFile(p_notebook, p_forced)) {
            return false;
        }
        // win may be removed after closeFile()
        if (nrWin == splitter->count()) {
            ++i;
        }
    }
    updateWindowStatus();
    return true;
}

bool VEditArea::closeAllFiles(bool p_forced)
{
    int i = 0;
    while (i < splitter->count()) {
        VEditWindow *win = getWindow(i);
        if (!p_forced) {
            setCurrentWindow(i, false);
        }
        int nrWin = splitter->count();
        if (!win->closeAllFiles(p_forced)) {
            return false;
        }
        if (nrWin == splitter->count()) {
            ++i;
        }
    }
    updateWindowStatus();
    return true;
}

void VEditArea::editFile()
{
    VEditWindow *win = getWindow(curWindowIndex);
    win->editFile();
}

void VEditArea::saveFile()
{
    VEditWindow *win = getWindow(curWindowIndex);
    win->saveFile();
}

void VEditArea::readFile()
{
    VEditWindow *win = getWindow(curWindowIndex);
    win->readFile();
}

void VEditArea::saveAndReadFile()
{
    VEditWindow *win = getWindow(curWindowIndex);
    win->saveAndReadFile();
}

void VEditArea::splitWindow(VEditWindow *p_window, bool p_right)
{
    if (!p_window) {
        return;
    }

    int idx = splitter->indexOf(p_window);
    Q_ASSERT(idx > -1);
    if (p_right) {
        ++idx;
    } else {
        --idx;
        if (idx < 0) {
            idx = 0;
        }
    }

    insertSplitWindow(idx);
    setCurrentWindow(idx, true);
}

void VEditArea::splitCurrentWindow()
{
    if (curWindowIndex > -1) {
        splitWindow(getWindow(curWindowIndex));
    }
}

void VEditArea::handleRemoveSplitRequest(VEditWindow *curWindow)
{
    if (!curWindow || splitter->count() <= 1) {
        return;
    }
    int idx = splitter->indexOf(curWindow);

    // curWindow will be deleted later
    removeSplitWindow(curWindow);

    if (idx >= splitter->count()) {
        idx = splitter->count() - 1;
    }
    setCurrentWindow(idx, true);
}

void VEditArea::removeCurrentWindow()
{
    if (curWindowIndex > -1) {
        handleRemoveSplitRequest(getWindow(curWindowIndex));
    }
}

void VEditArea::mousePressEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        VEditWindow *win = getWindow(i);
        if (win->geometry().contains(pos, true)) {
            setCurrentWindow(i, true);
            break;
        }
    }
    QWidget::mousePressEvent(event);
}

void VEditArea::handleWindowFocused()
{
    QObject *winObject = sender();
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        if (splitter->widget(i) == winObject) {
            setCurrentWindow(i, false);
            break;
        }
    }
}

void VEditArea::handleWindowOutlineChanged(const VTableOfContent &p_outline)
{
    QObject *winObject = sender();
    if (splitter->widget(curWindowIndex) == winObject) {
        emit outlineChanged(p_outline);
    }
}

void VEditArea::handleWindowCurrentHeaderChanged(const VHeaderPointer &p_header)
{
    QObject *winObject = sender();
    if (splitter->widget(curWindowIndex) == winObject) {
        emit currentHeaderChanged(p_header);
    }
}

void VEditArea::scrollToHeader(const VHeaderPointer &p_header)
{
    VEditWindow *win = getCurrentWindow();
    if (win) {
        win->scrollToHeader(p_header);
    }
}

bool VEditArea::isFileOpened(const VFile *p_file)
{
    return !findTabsByFile(p_file).isEmpty();
}

void VEditArea::handleFileUpdated(const VFile *p_file, UpdateAction p_act)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateFileInfo(p_file, p_act);
    }
}

void VEditArea::handleDirectoryUpdated(const VDirectory *p_dir, UpdateAction p_act)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateDirectoryInfo(p_dir, p_act);
    }
}

void VEditArea::handleNotebookUpdated(const VNotebook *p_notebook)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateNotebookInfo(p_notebook);
    }
}

VEditTab *VEditArea::getCurrentTab() const
{
    if (curWindowIndex == -1) {
        return NULL;
    }

    VEditWindow *win = getWindow(curWindowIndex);
    return win->getCurrentTab();
}

VEditTab *VEditArea::getTab(int p_winIdx, int p_tabIdx) const
{
    VEditWindow *win = getWindow(p_winIdx);
    if (!win) {
        return NULL;
    }

    return win->getTab(p_tabIdx);
}

VEditTab *VEditArea::getTab(const VFile *p_file) const
{
    int nrWin = splitter->count();
    for (int winIdx = 0; winIdx < nrWin; ++winIdx) {
        VEditWindow *win = getWindow(winIdx);
        int tabIdx = win->findTabByFile(p_file);
        if (tabIdx != -1) {
            return win->getTab(tabIdx);
        }
    }

    return NULL;
}

QVector<VEditTabInfo> VEditArea::getAllTabsInfo() const
{
    QVector<VEditTabInfo> tabs;
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        tabs.append(getWindow(i)->getAllTabsInfo());
    }

    return tabs;
}

int VEditArea::windowIndex(const VEditWindow *p_window) const
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        if (p_window == getWindow(i)) {
            return i;
        }
    }
    return -1;
}

void VEditArea::moveTab(QWidget *p_widget, int p_fromIdx, int p_toIdx)
{
    int nrWin = splitter->count();
    if (!p_widget || p_fromIdx < 0 || p_fromIdx >= nrWin
        || p_toIdx < 0 || p_toIdx >= nrWin) {
        return;
    }
    qDebug() << "move widget" << p_widget << "from" << p_fromIdx << "to" << p_toIdx;
    if (!getWindow(p_toIdx)->addEditTab(p_widget)) {
        delete p_widget;
    }
}

// Only propogate the search in the IncrementalSearch case.
void VEditArea::handleFindTextChanged(const QString &p_text, uint p_options)
{
    VEditTab *tab = getCurrentTab();
    if (tab) {
        if (p_options & FindOption::IncrementalSearch) {
            tab->findText(p_text, p_options, true);
        }
    }
}

void VEditArea::handleFindOptionChanged(uint p_options)
{
    qDebug() << "find option changed" << p_options;
    g_config->setFindCaseSensitive(p_options & FindOption::CaseSensitive);
    g_config->setFindWholeWordOnly(p_options & FindOption::WholeWordOnly);
    g_config->setFindRegularExpression(p_options & FindOption::RegularExpression);
    g_config->setFindIncrementalSearch(p_options & FindOption::IncrementalSearch);
}

void VEditArea::handleFindNext(const QString &p_text, uint p_options,
                               bool p_forward)
{
    qDebug() << "find next" << p_text << p_options << p_forward;
    VEditTab *tab = getCurrentTab();
    if (tab) {
        tab->findText(p_text, p_options, false, p_forward);
    }
}

void VEditArea::handleReplace(const QString &p_text, uint p_options,
                              const QString &p_replaceText, bool p_findNext)
{
    qDebug() << "replace" << p_text << p_options << "with" << p_replaceText
             << p_findNext;
    VEditTab *tab = getCurrentTab();
    if (tab) {
        tab->replaceText(p_text, p_options, p_replaceText, p_findNext);
    }
}

void VEditArea::handleReplaceAll(const QString &p_text, uint p_options,
                                 const QString &p_replaceText)
{
    qDebug() << "replace all" << p_text << p_options << "with" << p_replaceText;
    VEditTab *tab = getCurrentTab();
    if (tab) {
        tab->replaceTextAll(p_text, p_options, p_replaceText);
    }
}

// Let VEditArea get focus after VFindReplaceDialog is closed.
void VEditArea::handleFindDialogClosed()
{
    if (curWindowIndex == -1) {
        setFocus();
    } else {
        getWindow(curWindowIndex)->focusWindow();
    }

    // Clear all the search highlight.
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->clearSearchedWordHighlight();
    }
}

QString VEditArea::getSelectedText()
{
    VEditTab *tab = getCurrentTab();
    if (tab) {
        return tab->getSelectedText();
    } else {
        return QString();
    }
}

int VEditArea::focusNextWindow(int p_biaIdx)
{
    if (p_biaIdx == 0) {
        return curWindowIndex;
    }
    int newIdx = curWindowIndex + p_biaIdx;
    if (newIdx < 0) {
        newIdx = 0;
    } else if (newIdx >= splitter->count()) {
        newIdx = splitter->count() - 1;
    }
    if (newIdx >= 0 && newIdx < splitter->count()) {
        setCurrentWindow(newIdx, true);
    } else {
        newIdx = -1;
    }
    return newIdx;
}

void VEditArea::moveCurrentTabOneSplit(bool p_right)
{
    getWindow(curWindowIndex)->moveCurrentTabOneSplit(p_right);
}

VEditWindow *VEditArea::getCurrentWindow() const
{
    if (curWindowIndex < 0) {
        return NULL;
    }

    return getWindow(curWindowIndex);
}

void VEditArea::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
    V_ASSERT(m_keyMap.empty());
    V_ASSERT(m_naviLabels.empty());
}

void VEditArea::showNavigation()
{
    // Clean up.
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }

    m_naviLabels.clear();

    if (!isVisible()) {
        return;
    }

    // Generate labels for VEDitTab.
    int charIdx = 0;
    for (int i = 0; charIdx < 26 && i < splitter->count(); ++i) {
        VEditWindow *win = getWindow(i);
        QVector<TabNavigationInfo> tabInfos = win->getTabsNavigationInfo();
        for (int j = 0; charIdx < 26 && j < tabInfos.size(); ++j, ++charIdx) {
            QChar key('a' + charIdx);
            m_keyMap[key] = tabInfos[j].m_tab;
            QString str = QString(m_majorKey) + key;
            QLabel *label = new QLabel(str, win);
            label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
            label->show();
            label->move(tabInfos[j].m_topLeft);
            m_naviLabels.append(label);
        }
    }
}

void VEditArea::hideNavigation()
{
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }

    m_naviLabels.clear();
}

bool VEditArea::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (secondKey && !keyChar.isNull()) {
        secondKey = false;
        p_succeed = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            ret = true;
            static_cast<VEditTab *>(it.value())->focusTab();
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            secondKey = true;
        }
        ret = true;
    }

    return ret;
}

int VEditArea::openFiles(const QVector<VFileSessionInfo> &p_files)
{
    int nrOpened = 0;
    for (auto const & info : p_files) {
        QString filePath = VUtils::validFilePathToOpen(info.m_file);
        if (filePath.isEmpty()) {
            continue;
        }

        VFile *file = g_vnote->getFile(filePath);
        if (!file) {
            continue;
        }

        VEditTab *tab = openFile(file, info.m_mode, true);
        ++nrOpened;

        VEditTabInfo tabInfo;
        tabInfo.m_editTab = tab;
        info.toEditTabInfo(&tabInfo);

        tab->tryRestoreFromTabInfo(tabInfo);
    }

    return nrOpened;
}

void VEditArea::registerCaptainTargets()
{
    using namespace std::placeholders;

    VCaptain *captain = g_mainWin->getCaptain();

    captain->registerCaptainTarget(tr("ActivateTab1"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab1"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 1));
    captain->registerCaptainTarget(tr("ActivateTab2"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab2"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 2));
    captain->registerCaptainTarget(tr("ActivateTab3"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab3"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 3));
    captain->registerCaptainTarget(tr("ActivateTab4"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab4"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 4));
    captain->registerCaptainTarget(tr("ActivateTab5"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab5"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 5));
    captain->registerCaptainTarget(tr("ActivateTab6"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab6"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 6));
    captain->registerCaptainTarget(tr("ActivateTab7"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab7"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 7));
    captain->registerCaptainTarget(tr("ActivateTab8"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab8"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 8));
    captain->registerCaptainTarget(tr("ActivateTab9"),
                                   g_config->getCaptainShortcutKeySequence("ActivateTab9"),
                                   this,
                                   std::bind(activateTabByCaptain, _1, _2, 9));
    captain->registerCaptainTarget(tr("AlternateTab"),
                                   g_config->getCaptainShortcutKeySequence("AlternateTab"),
                                   this,
                                   alternateTabByCaptain);
    captain->registerCaptainTarget(tr("OpenedFileList"),
                                   g_config->getCaptainShortcutKeySequence("OpenedFileList"),
                                   this,
                                   showOpenedFileListByCaptain);
    captain->registerCaptainTarget(tr("ActivateSplitLeft"),
                                   g_config->getCaptainShortcutKeySequence("ActivateSplitLeft"),
                                   this,
                                   activateSplitLeftByCaptain);
    captain->registerCaptainTarget(tr("ActivateSplitRight"),
                                   g_config->getCaptainShortcutKeySequence("ActivateSplitRight"),
                                   this,
                                   activateSplitRightByCaptain);
    captain->registerCaptainTarget(tr("MoveTabSplitLeft"),
                                   g_config->getCaptainShortcutKeySequence("MoveTabSplitLeft"),
                                   this,
                                   moveTabSplitLeftByCaptain);
    captain->registerCaptainTarget(tr("MoveTabSplitRight"),
                                   g_config->getCaptainShortcutKeySequence("MoveTabSplitRight"),
                                   this,
                                   moveTabSplitRightByCaptain);
    captain->registerCaptainTarget(tr("ActivateNextTab"),
                                   g_config->getCaptainShortcutKeySequence("ActivateNextTab"),
                                   this,
                                   activateNextTabByCaptain);
    captain->registerCaptainTarget(tr("ActivatePreviousTab"),
                                   g_config->getCaptainShortcutKeySequence("ActivatePreviousTab"),
                                   this,
                                   activatePreviousTabByCaptain);
    captain->registerCaptainTarget(tr("VerticalSplit"),
                                   g_config->getCaptainShortcutKeySequence("VerticalSplit"),
                                   this,
                                   verticalSplitByCaptain);
    captain->registerCaptainTarget(tr("RemoveSplit"),
                                   g_config->getCaptainShortcutKeySequence("RemoveSplit"),
                                   this,
                                   removeSplitByCaptain);
    captain->registerCaptainTarget(tr("MagicWord"),
                                   g_config->getCaptainShortcutKeySequence("MagicWord"),
                                   this,
                                   evaluateMagicWordsByCaptain);
    captain->registerCaptainTarget(tr("ApplySnippet"),
                                   g_config->getCaptainShortcutKeySequence("ApplySnippet"),
                                   this,
                                   applySnippetByCaptain);
    captain->registerCaptainTarget(tr("LivePreview"),
                                   g_config->getCaptainShortcutKeySequence("LivePreview"),
                                   this,
                                   toggleLivePreviewByCaptain);
}

bool VEditArea::activateTabByCaptain(void *p_target, void *p_data, int p_idx)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    VEditWindow *win = obj->getCurrentWindow();
    if (win) {
        if (win->activateTab(p_idx)) {
            return false;
        }
    }

    return true;
}

bool VEditArea::alternateTabByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    VEditWindow *win = obj->getCurrentWindow();
    if (win) {
        if (win->alternateTab()) {
            return false;
        }
    }

    return true;
}

bool VEditArea::showOpenedFileListByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    VEditWindow *win = obj->getCurrentWindow();
    if (win) {
        if (win->showOpenedFileList()) {
            return false;
        }
    }

    return true;
}

bool VEditArea::activateSplitLeftByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    if (obj->focusNextWindow(-1) > -1) {
        return false;
    }

    return true;
}

bool VEditArea::activateSplitRightByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    if (obj->focusNextWindow(1) > -1) {
        return false;
    }

    return true;
}

bool VEditArea::moveTabSplitLeftByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    obj->moveCurrentTabOneSplit(false);
    return true;
}

bool VEditArea::moveTabSplitRightByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    obj->moveCurrentTabOneSplit(true);
    return true;
}

bool VEditArea::activateNextTabByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    VEditWindow *win = obj->getCurrentWindow();
    if (win) {
        win->focusNextTab(true);
        return false;
    }

    return true;
}

bool VEditArea::activatePreviousTabByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    VEditWindow *win = obj->getCurrentWindow();
    if (win) {
        win->focusNextTab(false);
        return false;
    }

    return true;
}

bool VEditArea::verticalSplitByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    obj->splitCurrentWindow();
    return false;
}

bool VEditArea::removeSplitByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    obj->removeCurrentWindow();

    QWidget *nextFocus = obj->getCurrentTab();
    if (nextFocus) {
        nextFocus->setFocus();
    } else {
        g_mainWin->getFileList()->setFocus();
    }

    return false;
}

bool VEditArea::evaluateMagicWordsByCaptain(void *p_target, void *p_data)
{
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    CaptainData *data = static_cast<CaptainData *>(p_data);

    VEditTab *tab = obj->getCurrentTab();
    if (tab
        && (data->m_focusWidgetBeforeCaptain == tab
            || tab->isAncestorOf(data->m_focusWidgetBeforeCaptain))) {
        tab->evaluateMagicWords();
    }

    return true;
}

bool VEditArea::applySnippetByCaptain(void *p_target, void *p_data)
{
    VEditArea *obj = static_cast<VEditArea *>(p_target);
    CaptainData *data = static_cast<CaptainData *>(p_data);

    VEditTab *tab = obj->getCurrentTab();
    if (tab
        && (data->m_focusWidgetBeforeCaptain == tab
            || tab->isAncestorOf(data->m_focusWidgetBeforeCaptain))) {
        tab->applySnippet();
    }

    return true;
}

bool VEditArea::toggleLivePreviewByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VEditArea *obj = static_cast<VEditArea *>(p_target);

    VEditTab *tab = obj->getCurrentTab();
    if (tab) {
        tab->toggleLivePreview();
    }

    return true;
}

void VEditArea::recordClosedFile(const VFileSessionInfo &p_file)
{
    for (auto it = m_lastClosedFiles.begin(); it != m_lastClosedFiles.end(); ++it) {
        if (it->m_file == p_file.m_file) {
            // Remove it.
            m_lastClosedFiles.erase(it);
            break;
        }
    }

    m_lastClosedFiles.push(p_file);
    qDebug() << "pushed closed file" << p_file.m_file;
}

void VEditArea::handleFileTimerTimeout()
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        // Check whether opened files have been changed outside.
        VEditWindow *win = getWindow(i);
        win->checkFileChangeOutside();

        if (m_autoSave) {
            win->saveAll();
        }
    }
}

QRect VEditArea::editAreaRect() const
{
    QRect rt = rect();
    int nrWin = splitter->count();
    if (nrWin > 0) {
        rt.setTopLeft(QPoint(0, getWindow(0)->tabBarHeight()));
    }

    return rt;
}
