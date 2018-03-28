#include "vsimplesearchinput.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QRegExpValidator>
#include <QRegExp>
#include <QLabel>

#include "vlineedit.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VSimpleSearchInput::VSimpleSearchInput(ISimpleSearch *p_obj, QWidget *p_parent)
    : QWidget(p_parent),
      m_obj(p_obj),
      m_inSearchMode(false),
      m_currentIdx(-1),
      m_wildCardEnabled(g_config->getEnableWildCardInSimpleSearch()),
      m_navigationKeyEnabled(false)
{
    if (m_wildCardEnabled) {
        m_matchFlags = Qt::MatchWildcard | Qt::MatchWrap | Qt::MatchRecursive;
    } else {
        m_matchFlags = Qt::MatchContains | Qt::MatchWrap | Qt::MatchRecursive;
    }

    m_searchEdit = new VLineEdit();
    m_searchEdit->setPlaceholderText(tr("Type to search"));
    m_searchEdit->setCtrlKEnabled(false);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &VSimpleSearchInput::handleEditTextChanged);

    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_searchEdit);
    m_searchEdit->setValidator(validator);
    m_searchEdit->installEventFilter(this);

    m_infoLabel = new QLabel();

    QLayout *layout = new QHBoxLayout();
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_infoLabel);
    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
}

void VSimpleSearchInput::clear()
{
    m_inSearchMode = false;
    clearSearch();
}

// If it is the / leader key to trigger search mode.
static bool isLeaderKey(int p_key, int p_modifiers)
{
    return p_key == Qt::Key_Slash && p_modifiers == Qt::NoModifier;
}

static bool isCharKey(int p_key, int p_modifiers)
{
    return p_key >= Qt::Key_A
           && p_key <= Qt::Key_Z
           && (p_modifiers == Qt::NoModifier || p_modifiers == Qt::ShiftModifier);
}

static bool isDigitKey(int p_key, int p_modifiers)
{
    return p_key >= Qt::Key_0
           && p_key <= Qt::Key_9
           && (p_modifiers == Qt::NoModifier || p_modifiers == Qt::KeypadModifier);
}

static QChar keyToChar(int p_key, int p_modifiers)
{
    if (isCharKey(p_key, p_modifiers)) {
        char ch = p_modifiers == Qt::ShiftModifier ? 'A' : 'a';
        return QChar(ch + (p_key - Qt::Key_A));
    } else if (isDigitKey(p_key, p_modifiers)) {
        return QChar('0' + (p_key - Qt::Key_0));
    }

    return QChar();
}

bool VSimpleSearchInput::tryHandleKeyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    Qt::KeyboardModifiers modifiers = p_event->modifiers();

    if (!m_inSearchMode) {
        // Try to trigger search mode.
        QChar ch;
        if (isCharKey(key, modifiers)
            || isDigitKey(key, modifiers)) {
            m_inSearchMode = true;
            ch = keyToChar(key, modifiers);
        } else if (isLeaderKey(key, modifiers)) {
            m_inSearchMode = true;
        }

        if (m_inSearchMode) {
            emit triggered(m_inSearchMode, false);

            clearSearch();
            m_searchEdit->setFocus();

            if (!ch.isNull()) {
                m_searchEdit->setText(ch);
            }

            return true;
        }
    } else {
        // Try to exit search mode.
        if (key == Qt::Key_Escape
            || (key == Qt::Key_BracketLeft
                && VUtils::isControlModifierForVim(modifiers))) {
            m_inSearchMode = false;
            emit triggered(m_inSearchMode, true);
            return true;
        }
    }

    // Ctrl+N/P to activate next hit item.
    if (VUtils::isControlModifierForVim(modifiers)
        && (key == Qt::Key_N || key == Qt::Key_P)) {
        int delta = key == Qt::Key_N ? 1 : -1;

        if (!m_inSearchMode) {
            m_inSearchMode = true;
            emit triggered(m_inSearchMode, false);
            m_searchEdit->setFocus();

            m_obj->highlightHitItems(m_hitItems);
        }

        if (!m_hitItems.isEmpty()) {
            m_currentIdx += delta;
            if (m_currentIdx < 0) {
                m_currentIdx = m_hitItems.size() - 1;
            } else if (m_currentIdx >= m_hitItems.size()) {
                m_currentIdx = 0;
            }

            m_obj->selectHitItem(currentItem());
        }

        return true;
    }

    // Up/Down Ctrl+K/J to navigate to next item.
    // QTreeWidget may not response to the key event if it does not have the focus.
    if (m_inSearchMode && m_navigationKeyEnabled) {
        if (key == Qt::Key_Down
            || key == Qt::Key_Up
            || (VUtils::isControlModifierForVim(modifiers)
                && (key == Qt::Key_J || key == Qt::Key_K))) {
            bool forward = true;
            if (key == Qt::Key_Up || key == Qt::Key_K) {
                forward = false;
            }

            m_obj->selectNextItem(forward);
            return true;
        }
    }

    return false;
}

void VSimpleSearchInput::clearSearch()
{
    m_searchEdit->clear();
    m_hitItems.clear();
    m_currentIdx = -1;
    m_obj->clearItemsHighlight();

    updateInfoLabel(0, m_obj->totalNumberOfItems());
}

bool VSimpleSearchInput::eventFilter(QObject *p_watched, QEvent *p_event)
{
    Q_UNUSED(p_watched);
    if (p_event->type() == QEvent::FocusOut) {
        QFocusEvent *eve = static_cast<QFocusEvent *>(p_event);
        if (eve->reason() != Qt::ActiveWindowFocusReason) {
            m_inSearchMode = false;
            emit triggered(m_inSearchMode, false);
        }
    }

    return QWidget::eventFilter(p_watched, p_event);
}

void VSimpleSearchInput::handleEditTextChanged(const QString &p_text)
{
    if (!m_inSearchMode) {
        goto exit;
    }

    if (p_text.isEmpty()) {
        clearSearch();
        m_obj->selectHitItem(NULL);
        goto exit;
    }

    if (m_wildCardEnabled) {
        QString wildcardText(p_text.size() * 2 + 1, '*');
        for (int i = 0, j = 1; i < p_text.size(); ++i, j += 2) {
            wildcardText[j] = p_text[i];
        }

        m_hitItems = m_obj->searchItems(wildcardText, m_matchFlags);
    } else {
        m_hitItems = m_obj->searchItems(p_text, m_matchFlags);
    }

    updateInfoLabel(m_hitItems.size(), m_obj->totalNumberOfItems());

    m_obj->highlightHitItems(m_hitItems);

    m_currentIdx = m_hitItems.isEmpty() ? -1 : 0;

    m_obj->selectHitItem(currentItem());

exit:
    emit inputTextChanged(p_text);
}

void VSimpleSearchInput::updateInfoLabel(int p_nrHit, int p_total)
{
    m_infoLabel->setText(tr("%1/%2").arg(p_nrHit).arg(p_total));
}
