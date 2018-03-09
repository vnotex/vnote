#include "vopenedlistmenu.h"
#include <QActionGroup>
#include <QAction>
#include <QFont>
#include <QVector>
#include <QTimer>
#include <QString>
#include <QStyleFactory>
#include <QWidgetAction>

#include "veditwindow.h"
#include "vnotefile.h"
#include "vedittab.h"
#include "vdirectory.h"
#include "utils/vutils.h"
#include "vbuttonmenuitem.h"
#include "utils/vimnavigationforwidget.h"
#include "vpalette.h"

extern VPalette *g_palette;

static const int c_cmdTime = 1 * 1000;

static bool fileComp(const VOpenedListMenu::ItemInfo &a,
                     const VOpenedListMenu::ItemInfo &b)
{
    QString notebooka, notebookb;
    if (a.file->getType() == FileType::Note) {
        notebooka = dynamic_cast<const VNoteFile *>(a.file)->getNotebookName().toLower();
    } else {
        notebooka = "EXTERNAL_FILES";
    }

    if (b.file->getType() == FileType::Note) {
        notebookb = dynamic_cast<const VNoteFile *>(b.file)->getNotebookName().toLower();
    } else {
        notebookb = "EXTERNAL_FILES";
    }

    if (notebooka < notebookb) {
        return true;
    } else if (notebooka > notebookb) {
        return false;
    } else {
        QString patha = a.file->fetchBasePath();
        QString pathb = b.file->fetchBasePath();
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
    : QMenu(p_editWin),
      m_editWin(p_editWin),
      m_cmdNum(0),
      m_accepted(false)
{
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
    m_accepted = false;

    int curTab = m_editWin->currentIndex();
    int nrTab = m_editWin->count();
    QVector<ItemInfo> files(nrTab);
    for (int i = 0; i < nrTab; ++i) {
        files[i].file = m_editWin->getTab(i)->getFile();
        files[i].index = i;
    }

    Q_ASSERT(!files.isEmpty());

    std::sort(files.begin(), files.end(), fileComp);

    QString notebook;
    const VDirectory *directory = NULL;
    for (int i = 0; i < nrTab; ++i) {
        QPointer<VFile> file = files[i].file;
        int index = files[i].index;

        // Whether add separator.
        QString curNotebook;
        const VDirectory *curDirectory = NULL;
        if (file->getType() == FileType::Note) {
            const VNoteFile *tmpFile = dynamic_cast<const VNoteFile *>((VFile *)file);
            curNotebook = tmpFile->getNotebookName();
            curDirectory = tmpFile->getDirectory();
        } else {
            curNotebook = "EXTERNAL_FILES";
        }

        QString separatorText;
        if (curNotebook != notebook
            || curDirectory != directory) {
            notebook = curNotebook;
            directory = curDirectory;
            QString dirName;
            if (directory) {
                dirName = directory->getName();
            }

            if (dirName.isEmpty()) {
                separatorText = QString("[%1]").arg(notebook);
            } else {
                separatorText = QString("[%1] %2").arg(notebook).arg(dirName);
            }

            addSeparator();
        }

        // Append the separator text to the end of the first item.
        QWidgetAction *wact = new QWidgetAction(this);
        wact->setData(QVariant::fromValue(file));
        VButtonMenuItem *w = new VButtonMenuItem(wact,
                                                 m_editWin->tabIcon(index),
                                                 m_editWin->tabText(index),
                                                 separatorText,
                                                 g_palette->color("buttonmenuitem_decoration_text_fg"),
                                                 this);
        w->setToolTip(generateDescription(file));
        wact->setDefaultWidget(w);

        if (index == curTab) {
            QFont boldFont = w->font();
            boldFont.setBold(true);
            w->setFont(boldFont);

            w->setFocus();
        }

        addAction(wact);
        m_seqActionMap[index + c_tabSequenceBase] = wact;
    }
}

QString VOpenedListMenu::generateDescription(const VFile *p_file) const
{
    if (!p_file) {
        return "";
    }

    // [Notebook]path
    if (p_file->getType() == FileType::Note) {
        const VNoteFile *tmpFile = dynamic_cast<const VNoteFile *>(p_file);
        return QString("[%1] %2").arg(tmpFile->getNotebookName()).arg(tmpFile->fetchPath());
    } else {
        return QString("%1").arg(p_file->fetchPath());
    }
}

void VOpenedListMenu::handleItemTriggered(QAction *p_action)
{
    if (p_action) {
        QPointer<VFile> file = p_action->data().value<QPointer<VFile>>();
        if (file) {
            m_accepted = true;
            emit fileTriggered(file);
        }
    }

    hide();
}

void VOpenedListMenu::keyPressEvent(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(this,
                                                          p_event)) {
        m_cmdTimer->stop();
        m_cmdNum = 0;
        return;
    }

    int key = p_event->key();
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
