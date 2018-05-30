#include "vlistwidget.h"

#include <QKeyEvent>
#include <QCoreApplication>
#include <QSet>
#include <QScrollBar>

#include "utils/vutils.h"
#include "utils/vimnavigationforwidget.h"
#include "vstyleditemdelegate.h"
#include "vpalette.h"

extern VPalette *g_palette;

VListWidget::VListWidget(QWidget *p_parent)
    : QListWidget(p_parent),
      ISimpleSearch(),
      m_fitContent(false)
{
    m_searchInput = new VSimpleSearchInput(this, this);
    connect(m_searchInput, &VSimpleSearchInput::triggered,
            this, &VListWidget::handleSearchModeTriggered);

    m_searchInput->hide();

    m_delegate = new VStyledItemDelegate(this, NULL);
    setItemDelegate(m_delegate);
}

void VListWidget::keyPressEvent(QKeyEvent *p_event)
{
    if (m_searchInput->tryHandleKeyPressEvent(p_event)) {
        return;
    }

    if (VimNavigationForWidget::injectKeyPressEventForVim(this, p_event)) {
        return;
    }

    QListWidget::keyPressEvent(p_event);
}

void VListWidget::clearAll()
{
    m_searchInput->clear();
    setSearchInputVisible(false);

    QListWidget::clear();
}

void VListWidget::setSearchInputVisible(bool p_visible)
{
    m_searchInput->setVisible(p_visible);

    int bottomMargin = 0;
    if (p_visible) {
        bottomMargin = m_searchInput->height();
    }

    setViewportMargins(0, 0, 0, bottomMargin);
}

void VListWidget::resizeEvent(QResizeEvent *p_event)
{
    QListWidget::resizeEvent(p_event);

    QRect rect = contentsRect();
    int width = rect.width();
    QScrollBar *vbar = verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
        width -= vbar->width();
    }

    int y = rect.bottom() - m_searchInput->height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && (hbar->minimum() != hbar->maximum())) {
        y -= hbar->height();
    }

    m_searchInput->setGeometry(QRect(rect.left(),
                                     y,
                                     width,
                                     m_searchInput->height()));
}

void VListWidget::handleSearchModeTriggered(bool p_inSearchMode, bool p_focus)
{
    if (p_inSearchMode) {
        setSearchInputVisible(p_inSearchMode);
    } else {
        // Hiding search input will make QWebEngine get focus which will consume
        // the Esc key sequence by mistake.
        if (p_focus) {
            setFocus();
        }

        setSearchInputVisible(p_inSearchMode);
        clearItemsHighlight();
    }

    if (p_focus) {
        setFocus();
    }
}

QList<void *> VListWidget::searchItems(const QString &p_text,
                                       Qt::MatchFlags p_flags) const
{
    QList<QListWidgetItem *> items = findItems(p_text, p_flags);

    QList<void *> res;
    res.reserve(items.size());
    for (int i = 0; i < items.size(); ++i) {
        if (items[i]->type() == ItemTypeSeparator) {
            continue;
        }

        res.append(items[i]);
    }

    return res;
}

void VListWidget::highlightHitItems(const QList<void *> &p_items)
{
    clearItemsHighlight();

    QSet<QModelIndex> hitIndexes;
    for (auto it : p_items) {
        QModelIndex index = indexFromItem(static_cast<QListWidgetItem *>(it));
        if (index.isValid()) {
            hitIndexes.insert(index);
        }
    }

    if (!hitIndexes.isEmpty()) {
        m_delegate->setHitItems(hitIndexes);
        update();
    }
}

void VListWidget::clearItemsHighlight()
{
    m_delegate->clearHitItems();
    update();
}

void VListWidget::selectHitItem(void *p_item)
{
    setCurrentItem(static_cast<QListWidgetItem *>(p_item),
                   QItemSelectionModel::ClearAndSelect);
}

int VListWidget::totalNumberOfItems()
{
    return count();
}

void VListWidget::selectNextItem(bool p_forward)
{
    if (count() == 0) {
        return;
    }

    int cur = currentRow();
    cur = cur + (p_forward ? 1 : -1);
    if (cur < 0) {
        cur = 0;
    } else if (cur >= count()) {
        cur = count() - 1;
    }

    setCurrentRow(cur);
}

void VListWidget::sortListWidget(QListWidget *p_list, const QVector<int> &p_sortedIdx)
{
    int cnt = p_list->count();
    Q_ASSERT(cnt == p_sortedIdx.size());

    QVector<QListWidgetItem *> sortedItems(cnt);
    for (int i = 0; i < cnt; ++i) {
        sortedItems[i] = p_list->item(p_sortedIdx[i]);
    }

    for (int i = 0; i < cnt; ++i) {
        QListWidgetItem *it = p_list->takeItem(p_list->row(sortedItems[i]));
        p_list->insertItem(i, it);
    }
}

QSize VListWidget::sizeHint() const
{
    int cnt = count();
    if (cnt == 0 || !m_fitContent) {
        return QListWidget::sizeHint();
    } else {
        // Adjust size to content.
        int hei = 0;
        int wid = sizeHintForColumn(0) + 10;
        for (int i = 0; i < cnt; ++i) {
            hei += sizeHintForRow(i);
        }

        hei += 2 * cnt;

        // Scrollbar.
        QScrollBar *verBar = verticalScrollBar();
        QScrollBar *horBar = horizontalScrollBar();
        if (verBar && (verBar->minimum() != verBar->maximum())) {
            wid += verBar->width();
        }

        if (horBar && (horBar->minimum() != horBar->maximum())) {
            hei += horBar->height();
        }

        return QSize(wid, hei);
    }
}

QListWidgetItem *VListWidget::createSeparatorItem(const QString &p_text)
{
    QListWidgetItem *item = new QListWidgetItem(p_text, NULL, ItemTypeSeparator);
    item->setFlags(Qt::NoItemFlags);
    return item;
}

bool VListWidget::isSeparatorItem(const QListWidgetItem *p_item)
{
    return p_item->type() == ItemTypeSeparator;
}

void VListWidget::moveItem(int p_srcRow, int p_destRow)
{
    QListWidgetItem *it = takeItem(p_srcRow);
    if (it) {
        insertItem(p_destRow, it);
    }
}
