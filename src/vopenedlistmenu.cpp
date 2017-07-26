#include "vopenedlistmenu.h"
#include <QActionGroup>
#include <QAction>
#include <QFont>
#include <QVector>
#include <QTimer>
#include <QString>
#include <QStyleFactory>

#include "veditwindow.h"
#include "vfile.h"
#include "vedittab.h"
#include "vdirectory.h"
#include "utils/vutils.h"

static const int c_cmdTime = 1 * 1000;

static bool fileComp(const VOpenedListMenu::ItemInfo &a,
                     const VOpenedListMenu::ItemInfo &b)
{
    QString notebooka = a.file->getNotebookName().toLower();
    QString notebookb = b.file->getNotebookName().toLower();
    if (notebooka < notebookb) {
        return true;
    } else if (notebooka > notebookb) {
        return false;
    } else {
        QString patha = a.file->retriveBasePath();
        QString pathb = b.file->retriveBasePath();
#if defined(Q_OS_WIN)
        patha = patha.toLower();
        pathb = pathb.toLower();
#endif
        if (patha == pathb) {
            return a.index < b.index;
        } else {
            return patha < pathb;
        }
    }
}

VOpenedListMenu::VOpenedListMenu(VEditWindow *p_editWin)
    : QMenu(p_editWin), m_editWin(p_editWin), m_cmdNum(0)
{
    // Force to display separator text on Windows and macOS.
    setStyle(QStyleFactory::create("Fusion"));
    int separatorHeight = 20 * VUtils::calculateScaleFactor();
    QString style = QString("::separator { color: #009688; height: %1px; padding-top: 3px; }").arg(separatorHeight);
    setStyleSheet(style);

    setToolTipsVisible(true);

    m_cmdTimer = new QTimer(this);
    m_cmdTimer->setSingleShot(true);
    m_cmdTimer->setInterval(c_cmdTime);
    connect(m_cmdTimer, &QTimer::timeout,
            this, &VOpenedListMenu::cmdTimerTimeout);

    connect(this, &QMenu::aboutToShow,
            this, &VOpenedListMenu::updateOpenedList);
    connect(this, &QMenu::triggered,
            this, &VOpenedListMenu::handleItemTriggered);
}

void VOpenedListMenu::updateOpenedList()
{
    // Regenerate the opened list.
    m_seqActionMap.clear();
    clear();

    int curTab = m_editWin->currentIndex();
    int nrTab = m_editWin->count();
    QVector<ItemInfo> files(nrTab);
    for (int i = 0; i < nrTab; ++i) {
        files[i].file = m_editWin->getTab(i)->getFile();
        files[i].index = i;
    }

    std::sort(files.begin(), files.end(), fileComp);

    QString notebook;
    const VDirectory *directory = NULL;
    QFont sepFont;
    sepFont.setItalic(true);
    for (int i = 0; i < nrTab; ++i) {
        QPointer<VFile> file = files[i].file;
        int index = files[i].index;

        // Whether add separator.
        QString curNotebook = file->getNotebookName();
        if (curNotebook != notebook
            || file->getDirectory() != directory) {
            notebook = curNotebook;
            directory = file->getDirectory();
            QString dirName;
            if (directory) {
                dirName = directory->getName();
            }

            QString text;
            if (dirName.isEmpty()) {
                text = QString("[%1]").arg(notebook);
            } else {
                text = QString("[%1] %2").arg(notebook).arg(dirName);
            }

            QAction *sepAct = addSection(text);
            sepAct->setFont(sepFont);
        }

        QAction *action = new QAction(m_editWin->tabIcon(index),
                                      m_editWin->tabText(index));
        action->setToolTip(generateDescription(file));
        action->setData(QVariant::fromValue(file));
        if (index == curTab) {
            QFont boldFont;
            boldFont.setBold(true);
            action->setFont(boldFont);
        }
        addAction(action);
        m_seqActionMap[index + c_tabSequenceBase] = action;
    }
}

QString VOpenedListMenu::generateDescription(const VFile *p_file) const
{
    if (!p_file) {
        return "";
    }
    // [Notebook]path
    return QString("[%1] %2").arg(p_file->getNotebookName()).arg(p_file->retrivePath());
}

void VOpenedListMenu::handleItemTriggered(QAction *p_action)
{
    if (!p_action) {
        return;
    }
    QPointer<VFile> file = p_action->data().value<QPointer<VFile>>();
    emit fileTriggered(file);
}

void VOpenedListMenu::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    int modifiers = p_event->modifiers();
    switch (key) {
    case Qt::Key_0:
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
    {
        addDigit(key - Qt::Key_0);
        return;
    }

    case Qt::Key_BracketLeft:
    {
        m_cmdTimer->stop();
        m_cmdNum = 0;
        if (modifiers == Qt::ControlModifier) {
            hide();
            return;
        }
        break;
    }

    case Qt::Key_J:
    {
        m_cmdTimer->stop();
        m_cmdNum = 0;
        if (modifiers == Qt::ControlModifier) {
            QList<QAction *> acts = actions();
            if (acts.size() == 0) {
                return;
            }
            int idx = 0;
            QAction *act = activeAction();
            if (act) {
                for (int i = 0; i < acts.size(); ++i) {
                    if (acts.at(i) == act) {
                        idx = i + 1;
                        break;
                    }
                }
            }
            while (true) {
                if (idx >= acts.size()) {
                    idx = 0;
                }
                act = acts.at(idx);
                if (act->isSeparator() || !act->isVisible()) {
                    ++idx;
                } else {
                    break;
                }
            }
            setActiveAction(act);
            return;
        }
        break;
    }

    case Qt::Key_K:
    {
        m_cmdTimer->stop();
        m_cmdNum = 0;
        if (modifiers == Qt::ControlModifier) {
            QList<QAction *> acts = actions();
            if (acts.size() == 0) {
                return;
            }
            int idx = acts.size() - 1;
            QAction *act = activeAction();
            if (act) {
                for (int i = 0; i < acts.size(); ++i) {
                    if (acts.at(i) == act) {
                        idx = i - 1;
                        break;
                    }
                }
            }
            while (true) {
                if (idx < 0) {
                    idx = acts.size() - 1;
                }
                act = acts.at(idx);
                if (act->isSeparator() || !act->isVisible()) {
                    --idx;
                } else {
                    break;
                }
            }
            setActiveAction(act);
            return;
        }
        break;
    }

    default:
        m_cmdTimer->stop();
        m_cmdNum = 0;
        break;
    }
    QMenu::keyPressEvent(p_event);
}

void VOpenedListMenu::cmdTimerTimeout()
{
    if (m_cmdNum > 0) {
        triggerItem(m_cmdNum);
        m_cmdNum = 0;
    }
}

void VOpenedListMenu::addDigit(int p_digit)
{
    V_ASSERT(p_digit >= 0 && p_digit <= 9);
    m_cmdTimer->stop();
    m_cmdNum = m_cmdNum * 10 + p_digit;

    int totalItem = m_seqActionMap.size();
    // Try to trigger it ASAP.
    if (m_cmdNum > 0) {
        if (getNumOfDigit(m_cmdNum) == getNumOfDigit(totalItem)) {
            triggerItem(m_cmdNum);
            m_cmdNum = 0;
            return;
        }
        // Set active action to the candidate.
        auto it = m_seqActionMap.find(m_cmdNum);
        if (it != m_seqActionMap.end()) {
            QAction *act = it.value();
            setActiveAction(act);
        }
    }
    m_cmdTimer->start();
}

int VOpenedListMenu::getNumOfDigit(int p_num)
{
    int nrDigit = 1;
    while (true) {
        p_num /= 10;
        if (p_num == 0) {
            return nrDigit;
        } else {
            ++nrDigit;
        }
    }
}

void VOpenedListMenu::triggerItem(int p_seq)
{
    auto it = m_seqActionMap.find(p_seq);
    if (it != m_seqActionMap.end()) {
        QAction *act = it.value();
        act->trigger();
        hide();
    }
}
