#include <QtWidgets>
#include "veditarea.h"
#include "veditwindow.h"
#include "vedittab.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vfile.h"

VEditArea::VEditArea(VNote *vnote, QWidget *parent)
    : QWidget(parent), vnote(vnote), curWindowIndex(-1)
{
    setupUI();
}

void VEditArea::setupUI()
{
    splitter = new QSplitter(this);

    insertSplitWindow(0);
    setCurrentWindow(0, false);

    QHBoxLayout *mainLayout = new QHBoxLayout();
    mainLayout->addWidget(splitter);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setLayout(mainLayout);
}

void VEditArea::insertSplitWindow(int idx)
{
    VEditWindow *win = new VEditWindow(vnote);
    splitter->insertWidget(idx, win);
    connect(win, &VEditWindow::tabStatusChanged,
            this, &VEditArea::curTabStatusChanged);
    connect(win, &VEditWindow::requestSplitWindow,
            this, &VEditArea::handleSplitWindowRequest);
    connect(win, &VEditWindow::requestRemoveSplit,
            this, &VEditArea::handleRemoveSplitRequest);
    connect(win, &VEditWindow::getFocused,
            this, &VEditArea::handleWindowFocused);
    connect(win, &VEditWindow::outlineChanged,
            this, &VEditArea::handleOutlineChanged);
    connect(win, &VEditWindow::curHeaderChanged,
            this, &VEditArea::handleCurHeaderChanged);
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

// A given file can be opened in multiple split windows. A given file could be
// opened at most in one tab inside a window.
void VEditArea::openFile(VFile *p_file, OpenFileMode p_mode)
{
    if (!p_file) {
        return;
    }
    qDebug() << "VEditArea open" << p_file->getName() << (int)p_mode;

    // Find if it has been opened already
    int winIdx, tabIdx;
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
    setCurrentTab(winIdx, tabIdx, setFocus);
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
    if (curWindowIndex == windowIndex) {
        goto out;
    }
    curWindowIndex = windowIndex;
    if (curWindowIndex > -1 && setFocus) {
        getWindow(windowIndex)->focusWindow();
    }

out:
    // Update status
    updateWindowStatus();
}

void VEditArea::updateWindowStatus()
{
    if (curWindowIndex == -1) {
        Q_ASSERT(splitter->count() == 0);
        emit curTabStatusChanged(NULL, false);
        emit outlineChanged(VToc());
        emit curHeaderChanged(VAnchor());
        return;
    }
    VEditWindow *win = getWindow(curWindowIndex);
    win->requestUpdateTabStatus();
    win->requestUpdateOutline();
    win->requestUpdateCurHeader();
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

void VEditArea::handleSplitWindowRequest(VEditWindow *curWindow)
{
    if (!curWindow) {
        return;
    }
    int idx = splitter->indexOf(curWindow);
    qDebug() << "window" << idx << "requests split itself";
    insertSplitWindow(++idx);
    setCurrentWindow(idx, true);
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

void VEditArea::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "VEditArea press event" << event;
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

void VEditArea::handleOutlineChanged(const VToc &toc)
{
    QObject *winObject = sender();
    if (splitter->widget(curWindowIndex) == winObject) {
        emit outlineChanged(toc);
    }
}

void VEditArea::handleCurHeaderChanged(const VAnchor &anchor)
{
    QObject *winObject = sender();
    if (splitter->widget(curWindowIndex) == winObject) {
        emit curHeaderChanged(anchor);
    }
}

void VEditArea::handleOutlineItemActivated(const VAnchor &anchor)
{
    // Notice current window
    getWindow(curWindowIndex)->scrollCurTab(anchor);
}

bool VEditArea::isFileOpened(const VFile *p_file)
{
    return !findTabsByFile(p_file).isEmpty();
}

void VEditArea::handleFileUpdated(const VFile *p_file)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateFileInfo(p_file);
    }
}

void VEditArea::handleDirectoryUpdated(const VDirectory *p_dir)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateDirectoryInfo(p_dir);
    }
}

void VEditArea::handleNotebookUpdated(const VNotebook *p_notebook)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        getWindow(i)->updateNotebookInfo(p_notebook);
    }
}
