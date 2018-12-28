#include "vtexteditcompleter.h"

#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QScrollBar>
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>

#include "utils/vutils.h"
#include "veditor.h"
#include "vmainwindow.h"

extern VMainWindow *g_mainWin;

VTextEditCompleter::VTextEditCompleter(QObject *p_parent)
    : QCompleter(p_parent),
      m_initialized(false)
{
}

void VTextEditCompleter::performCompletion(const QStringList &p_words,
                                           const QString &p_prefix,
                                           Qt::CaseSensitivity p_cs,
                                           bool p_reversed,
                                           const QRect &p_rect,
                                           VEditor *p_editor)
{
    init();

    m_editor = p_editor;

    setWidget(m_editor->getEditor());

    m_model->setStringList(p_words);
    setCaseSensitivity(p_cs);
    setCompletionPrefix(p_prefix);

    int cnt = completionCount();
    if (cnt == 0) {
        finishCompletion();
        return;
    }

    selectRow(p_reversed ? cnt - 1 : 0);

    if (cnt == 1 && currentCompletion() == p_prefix) {
        finishCompletion();
        return;
    }

    g_mainWin->setCaptainModeEnabled(false);

    m_insertedCompletion = p_prefix;
    insertCurrentCompletion();

    auto pu = popup();
    QRect rt(p_rect);
    rt.setWidth(pu->sizeHintForColumn(0) + pu->verticalScrollBar()->sizeHint().width());
    complete(rt);
}

void VTextEditCompleter::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    m_model = new QStringListModel(this);
    setModel(m_model);

    popup()->setProperty("TextEdit", true);
    popup()->setItemDelegate(new QStyledItemDelegate(this));

    connect(this, static_cast<void(QCompleter::*)(const QString &)>(&QCompleter::activated),
            this, [this](const QString &p_text) {
                insertCompletion(p_text);
                finishCompletion();
            });
}

void VTextEditCompleter::selectNextCompletion(bool p_reversed)
{
    QModelIndex curIndex = popup()->currentIndex();
    if (p_reversed) {
        if (curIndex.isValid()) {
            int row = curIndex.row();
            if (row == 0) {
                setCurrentIndex(QModelIndex());
            } else {
                selectRow(row - 1);
            }
        } else {
            selectRow(completionCount() - 1);
        }
    } else {
        if (curIndex.isValid()) {
            int row = curIndex.row();
            if (!selectRow(row + 1)) {
                setCurrentIndex(QModelIndex());
            }
        } else {
            selectRow(0);
        }
    }
}

bool VTextEditCompleter::selectRow(int p_row)
{
    if (setCurrentRow(p_row)) {
        setCurrentIndex(currentIndex());
        return true;
    }

    return false;
}

void VTextEditCompleter::setCurrentIndex(QModelIndex p_index, bool p_select)
{
    auto pu = popup();
    if (!pu) {
        return;
    }

    if (!p_select) {
        pu->selectionModel()->setCurrentIndex(p_index, QItemSelectionModel::NoUpdate);
    } else {
        if (!p_index.isValid()) {
            pu->selectionModel()->clear();
        } else {
            pu->selectionModel()->setCurrentIndex(p_index, QItemSelectionModel::ClearAndSelect);
        }
    }

    p_index = pu->selectionModel()->currentIndex();
    if (!p_index.isValid()) {
        pu->scrollToTop();
    } else {
        pu->scrollTo(p_index);
    }
}

bool VTextEditCompleter::eventFilter(QObject *p_obj, QEvent *p_eve)
{
    switch (p_eve->type()) {
    case QEvent::KeyPress:
    {
        if (p_obj != popup() || !m_editor) {
            break;
        }

        bool exited = false;

        auto *ke = dynamic_cast<QKeyEvent *>(p_eve);
        const int key = ke->key();
        const int modifiers = ke->modifiers();
        switch (key) {
        case Qt::Key_N:
            V_FALLTHROUGH;
        case Qt::Key_P:
            if (VUtils::isControlModifierForVim(modifiers)) {
                selectNextCompletion(key == Qt::Key_P);
                insertCurrentCompletion();
                return true;
            }
            break;

        case Qt::Key_J:
            V_FALLTHROUGH;
        case Qt::Key_K:
            if (VUtils::isControlModifierForVim(modifiers)) {
                selectNextCompletion(key == Qt::Key_K);
                return true;
            }
            break;

        case Qt::Key_BracketLeft:
            if (!VUtils::isControlModifierForVim(modifiers)) {
                break;
            }
            V_FALLTHROUGH;
        case Qt::Key_Escape:
            exited = true;
            // Propogate this event to the editor widget for Vim mode.
            if (!m_editor->getVim()) {
                finishCompletion();
                return true;
            }
            break;

        case Qt::Key_E:
            if (VUtils::isControlModifierForVim(modifiers)) {
                cancelCompletion();
                return true;
            }
            break;

        case Qt::Key_Enter:
        case Qt::Key_Return:
        {
            if (m_insertedCompletion != currentCompletion()) {
                insertCurrentCompletion();
                finishCompletion();
                return true;
            } else {
                exited = true;
            }
            break;
        }

        default:
            break;
        }

        int cursorPos = -1;
        if (!exited) {
            cursorPos = m_editor->textCursorW().position();
        }

        bool ret = QCompleter::eventFilter(p_obj, p_eve);
        if (!exited) {
            // Detect if cursor position changed after key press.
            int pos = m_editor->textCursorW().position();
            if (pos == cursorPos - 1) {
                // Deleted one char.
                if (m_insertedCompletion.size() > 1) {
                    updatePrefix(m_editor->fetchCompletionPrefix());
                } else {
                    exited = true;
                }
            } else if (pos == cursorPos + 1) {
                // Added one char.
                QString prefix = m_editor->fetchCompletionPrefix();
                if (prefix.size() == m_insertedCompletion.size() + 1
                    && prefix.startsWith(m_insertedCompletion)) {
                    updatePrefix(prefix);
                } else {
                    exited = true;
                }
            } else if (pos != cursorPos) {
                exited = true;
            }
        }

        if (exited) {
            // finishCompletion() will do clean up. Must be called after QCompleter::eventFilter().
            finishCompletion();
        }

        return ret;
    }

    case QEvent::Hide:
    {
        // Completion exited.
        cleanUp();
        break;
    }

    default:
        break;
    }

    return QCompleter::eventFilter(p_obj, p_eve);
}

void VTextEditCompleter::insertCurrentCompletion()
{
    QString completion;
    QModelIndex curIndex = popup()->currentIndex();
    if (curIndex.isValid()) {
        completion = currentCompletion();
    } else {
        completion = completionPrefix();
    }

    insertCompletion(completion);
}

void VTextEditCompleter::insertCompletion(const QString &p_completion)
{
    if (m_insertedCompletion == p_completion) {
        return;
    }

    m_editor->insertCompletion(m_insertedCompletion, p_completion);
    m_insertedCompletion = p_completion;
}

void VTextEditCompleter::cancelCompletion()
{
    insertCompletion(completionPrefix());

    finishCompletion();
}

void VTextEditCompleter::cleanUp()
{
    // Do not clean up m_editor and m_insertedCompletion, since activated()
    // signal is after the HideEvent.
    setWidget(nullptr);
    g_mainWin->setCaptainModeEnabled(true);
}

void VTextEditCompleter::updatePrefix(const QString &p_prefix)
{
    m_insertedCompletion = p_prefix;
    setCompletionPrefix(p_prefix);

    int cnt = completionCount();
    if (cnt == 0) {
        finishCompletion();
    } else if (cnt == 1) {
        setCurrentRow(0);
        if (currentCompletion() == p_prefix) {
            finishCompletion();
        }
    }
}
