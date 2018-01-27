#include "vlistwidget.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QLabel>
#include "utils/vutils.h"

const QString searchPrefix("Search for: ");
//TODO: make the style configuable
const QString c_searchKeyStyle("border:none; background:#eaeaea; color:%1;");
const QString c_colorNotMatch("#fd676b");
const QString c_colorMatch("grey");

VListWidget::VListWidget(QWidget *parent):QListWidget(parent), m_isInSearch(false),
    m_curItemIdx(-1), m_curItem(nullptr)
{
    m_label = new QLabel(searchPrefix, this);
    //TODO: make the style configuable
    m_label->setStyleSheet(QString("color:gray;font-weight:bold;"));
    m_searchKey = new VLineEdit(this);
    m_searchKey->setStyleSheet(c_searchKeyStyle.arg(c_colorMatch));

    QGridLayout *mainLayout = new QGridLayout;
    QHBoxLayout *searchRowLayout = new QHBoxLayout;
    searchRowLayout->addWidget(m_label);
    searchRowLayout->addWidget(m_searchKey);

    mainLayout->addLayout(searchRowLayout, 0, 0, -1, 1, Qt::AlignBottom);
    setLayout(mainLayout);
    m_label->hide();
    m_searchKey->hide();

    connect(m_searchKey, &VLineEdit::textChanged,
            this, &VListWidget::handleSearchKeyChanged);

    m_delegateObj = new VItemDelegate(this);
    setItemDelegate(m_delegateObj);
}

void VListWidget::keyPressEvent(QKeyEvent *p_event)
{
    bool accept = false;
    int modifiers = p_event->modifiers();

    if (!m_isInSearch) {
        bool isChar = (p_event->key() >= Qt::Key_A && p_event->key() <= Qt::Key_Z)
                && (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier);
        bool isDigit = (p_event->key() >= Qt::Key_0 && p_event->key() <= Qt::Key_9)
                && (modifiers == Qt::NoModifier);
        m_isInSearch = isChar || isDigit;
    }

    bool moveUp = false;
    switch (p_event->key()) {
    case Qt::Key_J:
        if (VUtils::isControlModifierForVim(modifiers)) {
            // focus to next item/selection
            QKeyEvent *targetEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QCoreApplication::postEvent(this, targetEvent);
            return;
        }
        break;
    case Qt::Key_K:
        if (VUtils::isControlModifierForVim(modifiers)) {
            // focus to previous item/selection
            QKeyEvent *targetEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
            QCoreApplication::postEvent(this, targetEvent);
            return;
        }
        break;
    case Qt::Key_H:
        if (VUtils::isControlModifierForVim(modifiers)) {
            // Ctrl+H, delete one char
            accept = false;
        }
        break;
    case Qt::Key_F:
    case Qt::Key_B:
        // disable ctrl+f/b for the search key
        accept = VUtils::isControlModifierForVim(modifiers);
        break;
    case Qt::Key_Escape:
        m_isInSearch = false;
        break;
    case Qt::Key_Up:
        moveUp = true;
        // fall through
    case Qt::Key_Down:
        if (m_hitCount > 1) {
            int newIdx = m_curItemIdx;
            if (moveUp) {
                newIdx = (newIdx - 1 + m_hitCount) % m_hitCount;
            } else {
                newIdx = (newIdx  + 1) % m_hitCount;
            }
            if (newIdx != m_curItemIdx) {
                if (m_curItemIdx != -1) {
                    m_hitItems[m_curItemIdx]->setSelected(false);
                }

                m_curItemIdx = newIdx;
                m_curItem = m_hitItems[m_curItemIdx];
                selectItem(m_curItem);
            }
        }
        accept = true;
        break;
    }
    if (m_isInSearch) {
        enterSearchMode();
    } else {
        exitSearchMode();
    }

    if (!accept) {
        if (m_isInSearch) {
            m_searchKey->keyPressEvent(p_event);
        } else {
            QListWidget::keyPressEvent(p_event);
        }
    }
}

void VListWidget::enterSearchMode() {
    m_label->show();
    m_searchKey->show();
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void VListWidget::exitSearchMode(bool restoreSelection) {
    m_searchKey->clear();
    m_label->hide();
    m_searchKey->hide();
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (restoreSelection && m_curItem) {
        selectItem(m_curItem);
    }
}


void VListWidget::refresh() {
    m_isInSearch = false;
    m_hitItems = findItems("", Qt::MatchContains);
    m_hitCount = m_hitItems.count();

    for(const auto& it : selectedItems()) {
        it->setSelected(false);
    }

    if (m_hitCount > 0) {
        if (selectedItems().isEmpty()) {
            m_curItemIdx = 0;
            m_curItem = m_hitItems.first();
            selectItem(m_curItem);
        }
    } else {
        m_curItemIdx = -1;
        m_curItem = nullptr;
    }
}

void VListWidget::clear() {
    QListWidget::clear();
    m_hitCount = 0;
    m_hitItems.clear();
    m_isInSearch = false;
    m_curItem = nullptr;
    m_curItemIdx = 0;
    exitSearchMode();
}

void VListWidget::selectItem(QListWidgetItem *item) {
    if (item) {
        for(const auto& it : selectedItems()) {
            it->setSelected(false);
        }
        setCurrentItem(item);
    }
}

void VListWidget::handleSearchKeyChanged(const QString& key)
{
    m_delegateObj->setSearchKey(key);
    // trigger repaint & update
    update();

    m_hitItems = findItems(key, Qt::MatchContains);
    if (key.isEmpty()) {
        if (m_curItem) {
            m_curItemIdx = m_hitItems.indexOf(m_curItem);
        }
    } else {
       bool hasSearchResult = !m_hitItems.isEmpty();
       if (hasSearchResult) {
           m_searchKey->setStyleSheet(c_searchKeyStyle.arg(c_colorMatch));

           m_curItem = m_hitItems[0];
           setCurrentItem(m_curItem);
           m_curItemIdx = 0;
       } else {
           m_searchKey->setStyleSheet(c_searchKeyStyle.arg(c_colorNotMatch));
       }
    }
    m_hitCount = m_hitItems.count();
}
