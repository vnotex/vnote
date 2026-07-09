#include "windowsunitedentry.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QVariantMap>

#include "entrywidgetfactory.h"
#include "iviewwindownavigator.h"
#include "unitedentrymgr.h"
#include <core/servicelocator.h>

using namespace vnotex;

namespace {
// Keys for the child-item data stored in Qt::UserRole (QVariantMap).
const QString c_workspaceIdKey = QStringLiteral("workspaceId");
const QString c_bufferIdKey = QStringLiteral("bufferId");
} // namespace

WindowsUnitedEntry::WindowsUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                       QObject *p_parent)
    : IUnitedEntry("windows", tr("Open windows across workspaces"), p_mgr, p_parent) {
  Q_UNUSED(p_services);
}

void WindowsUnitedEntry::initOnFirstProcess() {
  m_tree = EntryWidgetFactory::createTreeWidget(1);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  connect(m_tree.data(), &QTreeWidget::itemActivated, this,
          &WindowsUnitedEntry::handleItemActivated);
}

void WindowsUnitedEntry::processInternal(
    const QString &p_args,
    const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) {
  setOngoing(true);

  auto *nav = m_mgr ? m_mgr->viewWindowNavigator() : nullptr;
  if (!nav) {
    QSharedPointer<QWidget> label = EntryWidgetFactory::createLabel(tr("No open windows"));
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  const auto entries = nav->listOpenWindows();
  if (entries.isEmpty()) {
    QSharedPointer<QWidget> label = EntryWidgetFactory::createLabel(tr("No open windows"));
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  const QString filter = p_args.trimmed();

  m_tree->clear();

  QTreeWidgetItem *currentItem = nullptr;
  QTreeWidgetItem *parentItem = nullptr;
  QString lastWorkspaceId;

  for (const auto &entry : entries) {
    // Apply case-insensitive filter on the window name.
    if (!filter.isEmpty() &&
        !entry.windowName.contains(filter, Qt::CaseInsensitive)) {
      continue;
    }

    // Start a new workspace parent when the workspace changes.
    if (!parentItem || entry.workspaceId != lastWorkspaceId) {
      // Drop the previous parent if it ended up with no matching children.
      if (parentItem && parentItem->childCount() == 0) {
        delete parentItem;
      }

      parentItem = new QTreeWidgetItem(m_tree.data());
      QString label = entry.workspaceName;
      if (label.isEmpty()) {
        label = tr("Workspace");
      }
      if (!entry.workspaceVisible) {
        label += tr(" (hidden)");
      }
      parentItem->setText(0, label);
      parentItem->setData(0, Qt::UserRole, entry.workspaceId);
      lastWorkspaceId = entry.workspaceId;
    }

    auto *child = new QTreeWidgetItem(parentItem);
    child->setText(0, entry.windowName);
    child->setIcon(0, entry.windowIcon);
    if (!entry.windowTitle.isEmpty()) {
      child->setToolTip(0, entry.windowTitle);
    }
    QVariantMap data;
    data.insert(c_workspaceIdKey, entry.workspaceId);
    data.insert(c_bufferIdKey, entry.bufferId);
    child->setData(0, Qt::UserRole, data);

    if (entry.windowCurrent) {
      currentItem = child;
    }
  }

  // Drop a trailing empty parent.
  if (parentItem && parentItem->childCount() == 0) {
    delete parentItem;
    parentItem = nullptr;
  }

  if (m_tree->topLevelItemCount() == 0) {
    QSharedPointer<QWidget> label = EntryWidgetFactory::createLabel(tr("No matching windows"));
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  // Expansion: expand all if enabled, otherwise expand the current workspace
  // (or the first workspace as a fallback).
  const bool expandAll = m_mgr && m_mgr->getExpandAllEnabled();
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    auto *top = m_tree->topLevelItem(i);
    if (expandAll) {
      top->setExpanded(true);
    }
  }
  if (!expandAll) {
    if (currentItem && currentItem->parent()) {
      currentItem->parent()->setExpanded(true);
    } else {
      m_tree->topLevelItem(0)->setExpanded(true);
    }
  }

  // Pre-select the current window (or the first top-level item).
  if (currentItem) {
    m_tree->setCurrentItem(currentItem);
    m_tree->scrollToItem(currentItem);
  } else {
    m_tree->setCurrentItem(m_tree->topLevelItem(0));
  }

  p_popupWidgetFunc(m_tree);
  finish();
}

void WindowsUnitedEntry::finish() {
  setOngoing(false);
  emit finished();
}

QSharedPointer<QWidget> WindowsUnitedEntry::currentPopupWidget() const { return m_tree; }

void WindowsUnitedEntry::handleItemActivated(QTreeWidgetItem *p_item, int p_column) {
  Q_UNUSED(p_column);

  if (!p_item) {
    return;
  }

  QTreeWidgetItem *target = p_item;
  // If a workspace parent was activated, focus its first child window.
  if (!p_item->parent()) {
    if (p_item->childCount() == 0) {
      return;
    }
    target = p_item->child(0);
  }

  const QVariantMap data = target->data(0, Qt::UserRole).toMap();
  const QString workspaceId = data.value(c_workspaceIdKey).toString();
  const QString bufferId = data.value(c_bufferIdKey).toString();
  if (workspaceId.isEmpty()) {
    return;
  }

  if (auto *nav = m_mgr ? m_mgr->viewWindowNavigator() : nullptr) {
    nav->focusWindow(workspaceId, bufferId);
  }

  emit itemActivated(true, false);
}
