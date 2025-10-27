#include "outlineviewer.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <utils/widgetutils.h>

#include "titlebar.h"
#include "treewidget.h"

#include "navigationmodemgr.h"
#include <core/configmgr.h>
#include <core/widgetconfig.h>

using namespace vnotex;

// Use static expansion when the level is 6.
#define STATIC_EXPANDED_LEVEL 6

OutlineViewer::OutlineViewer(const QString &p_title, QWidget *p_parent) : QFrame(p_parent) {
  setupUI(p_title);

  m_autoExpandedLevel = ConfigMgr::getInst().getWidgetConfig().getOutlineAutoExpandedLevel();

  m_expandTimer = new QTimer(this);
  m_expandTimer->setSingleShot(true);
  m_expandTimer->setInterval(1000);
  connect(m_expandTimer, &QTimer::timeout, this, [this]() {
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

void OutlineViewer::setupUI(const QString &p_title) {
  auto mainLayout = new QVBoxLayout(this);
  WidgetUtils::setContentsMargins(mainLayout);

  {
    auto titleBar = setupTitleBar(p_title, this);
    mainLayout->addWidget(titleBar);
  }

  m_tree = new TreeWidget(TreeWidget::Flag::None, this);
  TreeWidget::setupSingleColumnHeaderlessTree(m_tree, false, false);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  TreeWidget::showHorizontalScrollbar(m_tree);
  mainLayout->addWidget(m_tree);
  connect(m_tree, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *p_cur, QTreeWidgetItem *p_pre) {
            Q_UNUSED(p_pre);
            activateItem(p_cur);
          });
  connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *p_item, int p_col) {
    Q_UNUSED(p_col);
    // Will duplicate the signal. That's fine.
    activateItem(p_item, true);
  });

  setFocusProxy(m_tree);
}

NavigationModeWrapper<QTreeWidget, QTreeWidgetItem> *OutlineViewer::getNavigationModeWrapper() {
  if (!m_navigationWrapper) {
    m_navigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_tree));
  }
  return m_navigationWrapper.data();
}

TitleBar *OutlineViewer::setupTitleBar(const QString &p_title, QWidget *p_parent) {
  auto titleBar = new TitleBar(p_title, false, TitleBar::Action::Menu, p_parent);
  titleBar->setActionButtonsAlwaysShown(true);

  auto decreaseBtn = titleBar->addActionButton(QStringLiteral("decrease_outline_level.svg"),
                                               tr("Decrease Expansion Level"));
  connect(decreaseBtn, &QToolButton::clicked, this, [this]() {
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

  auto increaseBtn = titleBar->addActionButton(QStringLiteral("increase_outline_level.svg"),
                                               tr("Increase Expansion Level"));
  connect(increaseBtn, &QToolButton::clicked, this, [this]() {
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

  {
    auto act = titleBar->addMenuAction(tr("Section Number"), titleBar, [this](bool p_checked) {
      ConfigMgr::getInst().getWidgetConfig().setOutlineSectionNumberEnabled(p_checked);
      updateTree(true);
    });
    act->setCheckable(true);
    act->setChecked(ConfigMgr::getInst().getWidgetConfig().getOutlineSectionNumberEnabled());
  }

  return titleBar;
}

void OutlineViewer::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider) {
  if (m_provider == p_provider) {
    return;
  }

  if (m_provider) {
    disconnect(m_provider.data(), 0, this, 0);
    disconnect(this, 0, m_provider.data(), 0);
  }

  m_provider = p_provider;
  if (m_provider) {
    connect(m_provider.data(), &OutlineProvider::outlineChanged, this, [this]() {
      if (isVisible()) {
        updateOutline(m_provider->getOutline());
      }
    });
    connect(m_provider.data(), &OutlineProvider::currentHeadingChanged, this,
            [this]() { updateCurrentHeading(m_provider->getCurrentHeadingIndex()); });
  }

  if (isVisible()) {
    updateTree();
  }
}

void OutlineViewer::showEvent(QShowEvent *p_event) {
  QFrame::showEvent(p_event);

  updateTree();
}

void OutlineViewer::updateTree(bool p_force) {
  if (p_force) {
    m_outline.clear();
  }

  if (m_provider) {
    updateOutline(m_provider->getOutline());
    updateCurrentHeading(m_provider->getCurrentHeadingIndex());
  } else {
    updateOutline(nullptr);
    updateCurrentHeading(-1);
  }
}

void OutlineViewer::updateOutline(const QSharedPointer<Outline> &p_outline) {
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

void OutlineViewer::updateCurrentHeading(int p_idx) {
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

void OutlineViewer::updateTreeToOutline(QTreeWidget *p_tree, const Outline &p_outline) {
  p_tree->clear();
  if (p_outline.isEmpty()) {
    p_tree->update();
    return;
  }

  int sectionNumberBaseLevel = -1;
  const auto &widgetConfig = ConfigMgr::getInst().getWidgetConfig();
  if (widgetConfig.getOutlineSectionNumberEnabled()) {
    sectionNumberBaseLevel = p_outline.m_sectionNumberBaseLevel;
  }

  SectionNumber sectionNumber(7, 0);

  int idx = 0;
  renderTreeAtLevel(p_outline.m_headings, idx, 1, p_tree, nullptr, nullptr, sectionNumberBaseLevel,
                    sectionNumber, p_outline.m_sectionNumberEndingDot);
}

void OutlineViewer::renderTreeAtLevel(const QVector<Outline::Heading> &p_headings, int &p_index,
                                      int p_level, QTreeWidget *p_tree,
                                      QTreeWidgetItem *p_parentItem, QTreeWidgetItem *p_lastItem,
                                      int p_sectionNumberBaseLevel, SectionNumber &p_sectionNumber,
                                      bool p_sectionNumberEndingDot) {
  while (p_index < p_headings.size()) {
    const auto &heading = p_headings[p_index];
    if (heading.m_level < p_level) {
      return;
    }

    QTreeWidgetItem *item = nullptr;
    if (heading.m_level == p_level) {
      QString sectionStr;
      if (p_sectionNumberBaseLevel > 0) {
        OutlineProvider::increaseSectionNumber(p_sectionNumber, heading.m_level,
                                               p_sectionNumberBaseLevel);
        sectionStr = OutlineProvider::joinSectionNumber(p_sectionNumber, p_sectionNumberEndingDot);
      }

      if (p_parentItem) {
        item = new QTreeWidgetItem(p_parentItem);
      } else {
        item = new QTreeWidgetItem(p_tree);
      }

      fillTreeItem(item, heading, p_index, sectionStr);

      p_lastItem = item;
      ++p_index;
    } else {
      renderTreeAtLevel(p_headings, p_index, p_level + 1, p_tree, p_lastItem, nullptr,
                        p_sectionNumberBaseLevel, p_sectionNumber, p_sectionNumberEndingDot);
    }
  }
}

void OutlineViewer::fillTreeItem(QTreeWidgetItem *p_item, const Outline::Heading &p_heading,
                                 int p_index, const QString &p_sectionStr) {
  p_item->setData(Column::Name, Qt::UserRole, p_index);
  if (p_sectionStr.isEmpty()) {
    p_item->setText(Column::Name, p_heading.m_name);
  } else {
    p_item->setText(Column::Name, tr("%1 %2").arg(p_sectionStr, p_heading.m_name));
  }
  p_item->setToolTip(Column::Name, p_heading.m_name);
}

void OutlineViewer::highlightHeading(int p_idx) {
  if (p_idx == -1) {
    m_tree->setCurrentItem(nullptr);
    return;
  }

  auto item = TreeWidget::findItem(m_tree, p_idx);
  m_tree->setCurrentItem(item);
}

void OutlineViewer::expandTree(int p_level) {
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

void OutlineViewer::showLevel() {
  QToolTip::showText(mapToGlobal(QPoint(0, 0)), tr("Expansion level: %1").arg(m_autoExpandedLevel),
                     this);
}

void OutlineViewer::activateItem(QTreeWidgetItem *p_item, bool p_focus) {
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
