#include "voutline.h"

#include <QtWidgets>
#include <QCoreApplication>

#include "utils/vutils.h"
#include "vnote.h"
#include "vfile.h"
#include "vtreewidget.h"
#include "utils/viconutils.h"
#include "vconfigmanager.h"
#include "vmainwindow.h"

extern VNote *g_vnote;

extern VConfigManager *g_config;

extern VMainWindow *g_mainWin;

#define STATIC_EXPANDED_LEVEL 6

VOutline::VOutline(QWidget *parent)
    : QWidget(parent),
      VNavigationMode(),
      m_muted(false)
{
    setupUI();

    m_expandTimer = new QTimer(this);
    m_expandTimer->setSingleShot(true);
    m_expandTimer->setInterval(1000);
    connect(m_expandTimer, &QTimer::timeout,
            this, [this]() {
                // Auto adjust items after current header change.
                int level = g_config->getOutlineExpandedLevel();
                if (level == STATIC_EXPANDED_LEVEL) {
                    return;
                }

                expandTree(level);

                QTreeWidgetItem *curItem = m_tree->currentItem();
                if (curItem) {
                    m_tree->scrollToItem(curItem);
                }
            });
}

void VOutline::setupUI()
{
    m_deLevelBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/decrease_outline_level.svg"),
                                   "",
                                   this);
    m_deLevelBtn->setToolTip(tr("Decrease Expanded Level"));
    m_deLevelBtn->setProperty("FlatBtn", true);
    m_deLevelBtn->setEnabled(false);
    connect(m_deLevelBtn, &QPushButton::clicked,
            this, [this]() {
                int level = g_config->getOutlineExpandedLevel() - 1;
                if (level <= 0) {
                    level = 1;
                } else {
                    g_config->setOutlineExpandedLevel(level);
                    expandTree(level);
                }

                g_mainWin->showStatusMessage(tr("Set Outline Expanded Level to %1").arg(level));
            });

    m_inLevelBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/increase_outline_level.svg"),
                                   "",
                                   this);
    m_inLevelBtn->setToolTip(tr("Increase Expanded Level"));
    m_inLevelBtn->setProperty("FlatBtn", true);
    m_inLevelBtn->setEnabled(false);
    connect(m_inLevelBtn, &QPushButton::clicked,
            this, [this]() {
                int level = g_config->getOutlineExpandedLevel() + 1;
                if (level >= 7) {
                    level = 6;
                } else {
                    g_config->setOutlineExpandedLevel(level);
                    expandTree(level);
                }

                g_mainWin->showStatusMessage(tr("Set Outline Expanded Level to %1").arg(level));
            });

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(m_deLevelBtn);
    btnLayout->addWidget(m_inLevelBtn);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    m_tree = new VTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    // TODO: jump to the header when user click the same item twice.
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, &VOutline::handleCurrentItemChanged);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addLayout(btnLayout);
    layout->addWidget(m_tree);
    layout->setContentsMargins(3, 0, 3, 0);

    setLayout(layout);
}

void VOutline::updateOutline(const VTableOfContent &p_outline)
{
    if (p_outline == m_outline) {
        return;
    }

    // Clear current header
    m_currentHeader.clear();

    m_outline = p_outline;

    updateTreeFromOutline(m_tree, m_outline);

    updateButtonsState();

    expandTree(g_config->getOutlineExpandedLevel());
}

void VOutline::updateTreeFromOutline(QTreeWidget *p_treeWidget,
                                     const VTableOfContent &p_outline)
{
    p_treeWidget->clear();

    if (p_outline.isEmpty()) {
        return;
    }

    const QVector<VTableOfContentItem> &headers = p_outline.getTable();
    int idx = 0;
    updateTreeByLevel(p_treeWidget, headers, idx, NULL, NULL, 1);
}

void VOutline::updateTreeByLevel(QTreeWidget *p_treeWidget,
                                 const QVector<VTableOfContentItem> &p_headers,
                                 int &p_index,
                                 QTreeWidgetItem *p_parent,
                                 QTreeWidgetItem *p_last,
                                 int p_level)
{
    while (p_index < p_headers.size()) {
        const VTableOfContentItem &header = p_headers[p_index];
        QTreeWidgetItem *item;
        if (header.m_level == p_level) {
            if (p_parent) {
                item = new QTreeWidgetItem(p_parent);
            } else {
                item = new QTreeWidgetItem(p_treeWidget);
            }

            fillItem(item, header);

            p_last = item;
            ++p_index;
        } else if (header.m_level < p_level) {
            return;
        } else {
            updateTreeByLevel(p_treeWidget, p_headers, p_index, p_last, NULL, p_level + 1);
        }
    }
}

void VOutline::fillItem(QTreeWidgetItem *p_item, const VTableOfContentItem &p_header)
{
    p_item->setData(0, Qt::UserRole, p_header.m_index);
    p_item->setText(0, p_header.m_name);
    p_item->setToolTip(0, p_header.m_name);

    if (p_header.isEmpty()) {
        p_item->setForeground(0, QColor("grey"));
    }
}

void VOutline::expandTree(int p_expandedLevel)
{
    int topCount = m_tree->topLevelItemCount();
    if (topCount == 0) {
        return;
    }

    m_tree->collapseAll();

    // Get the base level.
    const VTableOfContentItem *header = getHeaderFromItem(m_tree->topLevelItem(0), m_outline);
    if (!header) {
        return;
    }

    int baseLevel = header->m_level;
    int levelToBeExpanded = p_expandedLevel - baseLevel;

    for (int i = 0; i < topCount; ++i) {
        expandTreeOne(m_tree->topLevelItem(i), levelToBeExpanded);
    }
}

void VOutline::expandTreeOne(QTreeWidgetItem *p_item, int p_levelToBeExpanded)
{
    if (p_levelToBeExpanded <= 0) {
        return;
    }

    // Expand this item.
    p_item->setExpanded(true);

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        expandTreeOne(p_item->child(i), p_levelToBeExpanded - 1);
    }
}

void VOutline::handleCurrentItemChanged(QTreeWidgetItem *p_curItem,
                                        QTreeWidgetItem * p_preItem)
{
    Q_UNUSED(p_preItem);

    if (!p_curItem) {
        return;
    }

    const VTableOfContentItem *header = getHeaderFromItem(p_curItem, m_outline);
    Q_ASSERT(header);
    m_currentHeader.update(m_outline.getFile(), header->m_index);

    if (!header->isEmpty() && !m_muted) {
        emit outlineItemActivated(m_currentHeader);
    }
}

void VOutline::updateCurrentHeader(const VHeaderPointer &p_header)
{
    if (p_header == m_currentHeader
        || !m_outline.isMatched(p_header)) {
        return;
    }

    // Item change should not emit the signal.
    m_muted = true;
    m_currentHeader = p_header;
    selectHeader(m_tree, m_outline, m_currentHeader);
    m_muted = false;

    m_expandTimer->start();
}

void VOutline::selectHeader(QTreeWidget *p_treeWidget,
                            const VTableOfContent &p_outline,
                            const VHeaderPointer &p_header)
{
    p_treeWidget->setCurrentItem(NULL);

    if (!p_outline.getItem(p_header)) {
        return;
    }

    int nrTop = p_treeWidget->topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectHeaderOne(p_treeWidget, p_treeWidget->topLevelItem(i), p_outline, p_header)) {
            return;
        }
    }
}

bool VOutline::selectHeaderOne(QTreeWidget *p_treeWidget,
                               QTreeWidgetItem *p_item,
                               const VTableOfContent &p_outline,
                               const VHeaderPointer &p_header)
{
    if (!p_item) {
        return false;
    }

    const VTableOfContentItem *header = getHeaderFromItem(p_item, p_outline);
    if (!header) {
        return false;
    }

    if (header->isMatched(p_header)) {
        p_treeWidget->setCurrentItem(p_item);
        return true;
    }

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectHeaderOne(p_treeWidget, p_item->child(i), p_outline, p_header)) {
            return true;
        }
    }

    return false;
}

void VOutline::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
        V_FALLTHROUGH;
    case Qt::Key_Enter:
    {
        QTreeWidgetItem *item = m_tree->currentItem();
        if (item) {
            item->setExpanded(!item->isExpanded());
        }

        return;
    }

    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void VOutline::showNavigation()
{
    VNavigationMode::showNavigation(m_tree);
}

bool VOutline::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    return VNavigationMode::handleKeyNavigation(m_tree,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

const VTableOfContentItem *VOutline::getHeaderFromItem(QTreeWidgetItem *p_item,
                                                       const VTableOfContent &p_outline)
{
    int index = p_item->data(0, Qt::UserRole).toInt();
    return p_outline.getItem(index);
}

void VOutline::focusInEvent(QFocusEvent *p_event)
{
    QWidget::focusInEvent(p_event);

    m_tree->setFocus();
}

void VOutline::updateButtonsState()
{
    bool empty = m_outline.isEmpty();
    m_deLevelBtn->setEnabled(!empty);
    m_inLevelBtn->setEnabled(!empty);
}
