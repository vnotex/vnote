#include "outlineviewer.h"

#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QTreeWidgetItem>
#include <QToolButton>
#include <QToolTip>
#include <QDebug>

#include "treewidget.h"
#include "titlebar.h"

#include "configmgr.h"
#include "widgetconfig.h"
#include "navigationmodemgr.h"

using namespace vnotex;

// Use static expansion when the level is 6.
#define STATIC_EXPANDED_LEVEL 6

OutlineViewer::OutlineViewer(const QString &p_title, QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI(p_title);

    m_autoExpandedLevel = ConfigMgr::getInst().getWidgetConfig().getOutlineAutoExpandedLevel();

    m_expandTimer = new QTimer(this);
    m_expandTimer->setSingleShot(true);
    m_expandTimer->setInterval(1000);
    connect(m_expandTimer, &QTimer::timeout,
            this, [this]() {
                // Auto adjust items after current heading change.
                if (m_autoExpandedLevel == STATIC_EXPANDED_LEVEL) {
                    return;
                }

                expandTree(m_autoExpandedLevel);

                auto curItem = m_tree->currentItem();
                if (curItem) {
                    m_tree->scrollToItem(curItem);
                }
            });
}

void OutlineViewer::setupUI(const QString &p_title)
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    {
        auto titleBar = setupTitleBar(p_title, this);
        mainLayout->addWidget(titleBar);
    }

    m_tree = new TreeWidget(TreeWidget::Flag::None, this);
    TreeWidget::setupSingleColumnHeaderlessTree(m_tree, false, false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    TreeWidget::showHorizontalScrollbar(m_tree);
    mainLayout->addWidget(m_tree);
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *p_cur, QTreeWidgetItem *p_pre) {
                Q_UNUSED(p_pre);
                activateItem(p_cur);
            });
    connect(m_tree, &QTreeWidget::itemClicked,
            this, [this](QTreeWidgetItem *p_item, int p_col) {
                Q_UNUSED(p_col);
                // Will duplicate the signal. That's fine.
                activateItem(p_item, true);
            });

    setFocusProxy(m_tree);
}

NavigationModeWrapper<QTreeWidget, QTreeWidgetItem> *OutlineViewer::getNavigationModeWrapper()
{
    if (!m_navigationWrapper) {
        m_navigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_tree));
    }
    return m_navigationWrapper.data();
}

TitleBar *OutlineViewer::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    auto titleBar = new TitleBar(p_title, TitleBar::Action::None, p_parent);

    auto decreaseBtn = titleBar->addActionButton(QStringLiteral("decrease_outline_level.svg"), tr("Decrease Expansion Level"));
    connect(decreaseBtn, &QToolButton::clicked,
            this, [this]() {
                auto &config = ConfigMgr::getInst().getWidgetConfig();
                m_autoExpandedLevel = config.getOutlineAutoExpandedLevel() - 1;
                if (m_autoExpandedLevel < 1) {
                    m_autoExpandedLevel = 1;
                } else {
                    config.setOutlineAutoExpandedLevel(m_autoExpandedLevel);
                    expandTree(m_autoExpandedLevel);
                }

                showLevel();
            });

    auto increaseBtn = titleBar->addActionButton(QStringLiteral("increase_outline_level.svg"), tr("Increase Expansion Level"));
    connect(increaseBtn, &QToolButton::clicked,
            this, [this]() {
                auto &config = ConfigMgr::getInst().getWidgetConfig();
                m_autoExpandedLevel = config.getOutlineAutoExpandedLevel() + 1;
                if (m_autoExpandedLevel > 6) {
                    m_autoExpandedLevel = 6;
                } else {
                    config.setOutlineAutoExpandedLevel(m_autoExpandedLevel);
                    expandTree(m_autoExpandedLevel);
                }

                showLevel();
            });

    return titleBar;
}

void OutlineViewer::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider)
{
    if (m_provider == p_provider) {
        return;
    }

    if (m_provider) {
        disconnect(m_provider.data(), 0, this, 0);
        disconnect(this, 0, m_provider.data(), 0);
    }

    m_provider = p_provider;
    if (m_provider) {
        connect(m_provider.data(), &OutlineProvider::outlineChanged,
                this, [this]() {
                    if (isVisible()) {
                        updateOutline(m_provider->getOutline());
                    }
                });
        connect(m_provider.data(), &OutlineProvider::currentHeadingChanged,
                this, [this]() {
                    updateCurrentHeading(m_provider->getCurrentHeadingIndex());
                });
        if (isVisible()) {
            updateOutline(m_provider->getOutline());
            updateCurrentHeading(m_provider->getCurrentHeadingIndex());
        }
    } else {
        updateOutline(nullptr);
        updateCurrentHeading(-1);
    }

}

void OutlineViewer::showEvent(QShowEvent *p_event)
{
    QFrame::showEvent(p_event);

    // Update the tree.
    if (m_provider) {
        updateOutline(m_provider->getOutline());
        updateCurrentHeading(m_provider->getCurrentHeadingIndex());
    } else {
        updateOutline(nullptr);
    }
}

void OutlineViewer::updateOutline(const QSharedPointer<Outline> &p_outline)
{
    if (!p_outline) {
        if (m_outline.isEmpty()) {
            return;
        }
        m_outline.clear();
    } else {
        if (m_outline == *p_outline) {
            return;
        }
        m_outline = *p_outline;
    }

    m_muted = true;
    updateTreeToOutline(m_tree, m_outline);

    expandTree(m_autoExpandedLevel);
    m_muted = false;
}

void OutlineViewer::updateCurrentHeading(int p_idx)
{
    if (m_currentHeadingIndex == p_idx) {
        return;
    }

    m_currentHeadingIndex = p_idx;
    if (m_currentHeadingIndex >= m_outline.m_headings.size()) {
        m_currentHeadingIndex = -1;
    }

    m_muted = true;
    highlightHeading(m_currentHeadingIndex);
    m_muted = false;

    m_expandTimer->start();
}

void OutlineViewer::updateTreeToOutline(QTreeWidget *p_tree, const Outline &p_outline)
{
    p_tree->clear();
    if (p_outline.isEmpty()) {
        p_tree->update();
        return;
    }

    int idx = 0;
    renderTreeAtLevel(p_outline.m_headings, idx, 1, p_tree, nullptr, nullptr);
}

void OutlineViewer::renderTreeAtLevel(const QVector<Outline::Heading> &p_headings,
                                      int &p_index,
                                      int p_level,
                                      QTreeWidget *p_tree,
                                      QTreeWidgetItem *p_parentItem,
                                      QTreeWidgetItem *p_lastItem)
{
    while (p_index < p_headings.size()) {
        const auto &heading = p_headings[p_index];
        QTreeWidgetItem *item = nullptr;
        if (heading.m_level == p_level) {
            if (p_parentItem) {
                item = new QTreeWidgetItem(p_parentItem);
            } else {
                item = new QTreeWidgetItem(p_tree);
            }

            fillTreeItem(item, heading, p_index);

            p_lastItem = item;
            ++p_index;
        } else if (heading.m_level < p_level) {
            return;
        } else {
            renderTreeAtLevel(p_headings, p_index, p_level + 1, p_tree, p_lastItem, nullptr);
        }
    }
}

void OutlineViewer::fillTreeItem(QTreeWidgetItem *p_item, const Outline::Heading &p_heading, int p_index)
{
    p_item->setData(Column::Name, Qt::UserRole, p_index);
    p_item->setText(Column::Name, p_heading.m_name);
    p_item->setToolTip(Column::Name, p_heading.m_name);
}

void OutlineViewer::highlightHeading(int p_idx)
{
    if (p_idx == -1) {
        m_tree->setCurrentItem(nullptr);
        return;
    }

    auto item = TreeWidget::findItem(m_tree, p_idx);
    m_tree->setCurrentItem(item);
}

void OutlineViewer::expandTree(int p_level)
{
    int cnt = m_tree->topLevelItemCount();
    if (cnt == 0) {
        return;
    }

    // Get the base level from the first heading.
    int baseLevel = m_outline.m_headings[0].m_level;
    int delta = p_level - baseLevel;
    if (delta <= 0) {
        m_tree->collapseAll();
    } else {
        m_tree->expandToDepth(delta - 1);
    }
}

void OutlineViewer::showLevel()
{
    QToolTip::showText(mapToGlobal(QPoint(0, 0)), tr("Expansion level: %1").arg(m_autoExpandedLevel), this);
}

void OutlineViewer::activateItem(QTreeWidgetItem *p_item, bool p_focus)
{
    if (!p_item || m_tree->selectedItems().isEmpty()) {
        return;
    }

    m_currentHeadingIndex = p_item->data(Column::Name, Qt::UserRole).toInt();
    if (m_currentHeadingIndex != -1 && !m_muted) {
        emit m_provider->headingClicked(m_currentHeadingIndex);
        if (p_focus) {
            emit focusViewArea();
        }
    }
}
