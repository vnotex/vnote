#include <QtWidgets>
#include "veditarea.h"
#include "veditwindow.h"
#include "vedittab.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vfile.h"

VEditArea::VEditArea(VNote *vnote, QWidget *parent)
    : QWidget(parent), vnote(vnote), curWindowIndex(0)
{
    setupUI();
}

void VEditArea::setupUI()
{
    splitter = new QSplitter(this);

    // Add a window
    insertSplitWindow(0);
    setCurrentWindow(0, true);

    QHBoxLayout *mainLayout = new QHBoxLayout();
    mainLayout->addWidget(splitter);
    mainLayout->setContentsMargins(0, 0, 0, 0);

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

    int nrWin = splitter->count();
    if (nrWin == 1) {
        // Disable removing split
        win->setRemoveSplitEnable(false);
    } else {
        // Enable removing split
        for (int i = 0; i < nrWin; ++i) {
            getWindow(i)->setRemoveSplitEnable(true);
        }
    }
}

void VEditArea::removeSplitWindow(VEditWindow *win)
{
    if (!win) {
        return;
    }
    win->hide();
    // Should be deleted later
    win->deleteLater();

    int nrWin = splitter->count();
    if (nrWin == 2) {
        // Disable removing split
        getWindow(0)->setRemoveSplitEnable(false);
        getWindow(1)->setRemoveSplitEnable(false);
    } else {
        // Enable removing split
        for (int i = 0; i < nrWin; ++i) {
            getWindow(i)->setRemoveSplitEnable(true);
        }
    }
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
    qDebug() << "current window" << windowIndex;
    curWindowIndex = windowIndex;
    if (setFocus) {
        getWindow(windowIndex)->focusWindow();
    }

out:
    // Update status
    updateWindowStatus();
}

void VEditArea::updateWindowStatus()
{
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
    int nrWin = splitter->count();
    bool ret = false;
    for (int i = 0; i < nrWin; ++i) {
        VEditWindow *win = getWindow(i);
        ret = ret || win->closeFile(p_file, p_forced);
    }
    updateWindowStatus();
    return ret;
}

bool VEditArea::closeAllFiles(bool p_forced)
{
    int nrWin = splitter->count();
    for (int i = 0; i < nrWin; ++i) {
        VEditWindow *win = getWindow(i);
        setCurrentWindow(i, false);
        if (!win->closeAllFiles(p_forced)) {
            return false;
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

    removeSplitWindow(curWindow);

    if (idx >= splitter->count()) {
        idx = splitter->count() - 1;
    }

    // At least one split window left
    Q_ASSERT(idx >= 0);
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
