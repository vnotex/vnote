#include "tagexplorer.h"

#include <QAbstractItemModel>
#include <QCompleter>
#include <QDebug>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "propertydefs.h"
#include "titlebar.h"

#include <core/configmgr.h>
#include <core/fileopenparameters.h>
#include <core/notebookmgr.h>
#include <core/vnotex.h>
#include <core/widgetconfig.h>
#include <notebook/notebook.h>
#include <notebook/tagi.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

#include "dialogs/newtagdialog.h"
#include "dialogs/renametagdialog.h"
#include "listwidget.h"
#include "mainwindow.h"
#include "messageboxhelper.h"
#include "navigationmodemgr.h"
#include "notebooknodeexplorer.h"
#include "treewidget.h"
#include "widgetsfactory.h"

using namespace vnotex;

TagExplorer::TagExplorer(QWidget *p_parent) : QFrame(p_parent) {
  initIcons();

  setupUI();
}

void TagExplorer::initIcons() {
  const auto &themeMgr = VNoteX::getInst().getThemeMgr();
  m_tagIcon = IconUtils::fetchIcon(themeMgr.getIconFile(QStringLiteral("tag.svg")));
  m_nodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(QStringLiteral("file_node.svg")));
}

void TagExplorer::setupUI() {
  auto mainLayout = new QVBoxLayout(this);
  WidgetUtils::setContentsMargins(mainLayout);

  setupTitleBar(this);
  mainLayout->addWidget(m_titleBar);

  m_tagSearchEdit = WidgetsFactory::createComboBox(this);
  m_tagSearchEdit->setEditable(true);
  m_tagSearchEdit->setLineEdit(WidgetsFactory::createLineEdit(this));
  m_tagSearchEdit->lineEdit()->setProperty(PropertyDefs::c_embeddedLineEdit, true);
  m_tagSearchEdit->lineEdit()->setPlaceholderText(tr(""));
  m_tagSearchEdit->lineEdit()->setClearButtonEnabled(true);
  m_tagSearchEdit->completer()->setCaseSensitivity(Qt::CaseSensitive);

  connect(m_tagSearchEdit->lineEdit(), &QLineEdit::textChanged, this, &TagExplorer::filterTags);

  mainLayout->addWidget(m_tagSearchEdit);

  m_splitter = new QSplitter(this);
  mainLayout->addWidget(m_splitter);

  setTwoColumnsEnabled(ConfigMgr::getInst().getWidgetConfig().getTagExplorerTwoColumnsEnabled());

  setupTagTree(m_splitter);
  m_splitter->addWidget(m_tagTree);

  setupNodeList(m_splitter);
  m_splitter->addWidget(m_nodeList);

  setFocusProxy(m_tagTree);
}

void TagExplorer::setupTitleBar(QWidget *p_parent) {
  m_titleBar = new TitleBar(QString(), false, TitleBar::Action::Menu, p_parent);
  m_titleBar->setActionButtonsAlwaysShown(true);

  auto twoColumnsAct =
      m_titleBar->addMenuAction(tr("Two Columns"), m_titleBar, [this](bool p_checked) {
        ConfigMgr::getInst().getWidgetConfig().setTagExplorerTwoColumnsEnabled(p_checked);
        setTwoColumnsEnabled(p_checked);
      });
  twoColumnsAct->setCheckable(true);
  twoColumnsAct->setChecked(
      ConfigMgr::getInst().getWidgetConfig().getTagExplorerTwoColumnsEnabled());
}

void TagExplorer::setTwoColumnsEnabled(bool p_enabled) {
  if (m_splitter) {
    m_splitter->setOrientation(p_enabled ? Qt::Horizontal : Qt::Vertical);
  }
}

void TagExplorer::setupTagTree(QWidget *p_parent) {
  auto timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, this, &TagExplorer::activateTagItem);

  m_tagTree = new TreeWidget(TreeWidget::ClickSpaceToClearSelection, p_parent);
  TreeWidget::setupSingleColumnHeaderlessTree(m_tagTree, true, false);
  TreeWidget::showHorizontalScrollbar(m_tagTree);
  m_tagTree->setDragDropMode(QAbstractItemView::InternalMove);
  m_tagTree->setSelectionMode(QAbstractItemView::MultiSelection);
  connect(m_tagTree, &QTreeWidget::itemSelectionChanged, this, [this, timer]() {
    auto selectedItems = m_tagTree->selectedItems();

    // Enable all items first
    std::function<void(QTreeWidgetItem *)> enableAllItems = [&](QTreeWidgetItem *p_item) {
      p_item->setDisabled(false);
      for (int i = 0; i < p_item->childCount(); ++i) {
        enableAllItems(p_item->child(i));
      }
    };
    for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i) {
      enableAllItems(m_tagTree->topLevelItem(i));
    }
    // no-bold when no item selected
    if (selectedItems.isEmpty()) {
      timer->start();
      std::function<void(QTreeWidgetItem *)> unboldAllItems = [&](QTreeWidgetItem *p_item) {
        QFont font = p_item->font(Column::Name);
        font.setBold(false);
        p_item->setFont(Column::Name, font);
        for (int i = 0; i < p_item->childCount(); ++i) {
          unboldAllItems(p_item->child(i));
        }
      };
      for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i) {
        unboldAllItems(m_tagTree->topLevelItem(i));
      }
      return;
    }

    // Get common nodes for selected tags
    QSet<QString> commonNodes;
    auto tagI = m_notebook->tag();
    if (Q_UNLIKELY(!tagI)) {
      return; // Tag interface not initialized
    }
    for (const auto &item : selectedItems) {
      auto tag = itemTag(item);
      auto nodes = tagI->findNodesOfTag(tag);
      if (commonNodes.isEmpty()) {
        commonNodes = QSet<QString>(nodes.begin(), nodes.end());
      } else {
        commonNodes.intersect(QSet<QString>(nodes.begin(), nodes.end()));
      }
    }

    // Disable incompatible tags
    // *Since parent tags affect the disabled state of child tags,
    // *keep the parent tag enabled (even if incompatible) when child tags are matched.
    std::function<void(QTreeWidgetItem *)> disableIncompatibleItems = [&](QTreeWidgetItem *p_item) {
      if (!selectedItems.contains(p_item)) {
        // Disable parent tag only if all child tags are incompatible.
        // Otherwise, keep it enabled.
        bool hasEnabledChild = false;
        for (int i = 0; i < p_item->childCount(); ++i) {
          auto child = p_item->child(i);
          disableIncompatibleItems(child);
          if (!child->isDisabled()) {
            hasEnabledChild = true;
          }
        }

        if (!hasEnabledChild) {
          auto tag = itemTag(p_item);
          auto nodes = tagI->findNodesOfTag(tag);
          QSet<QString> nodeSet(nodes.begin(), nodes.end());
          if (!nodeSet.intersects(commonNodes)) {
            p_item->setDisabled(true);
          }
        }
        return;
      }

      for (int i = 0; i < p_item->childCount(); ++i) {
        disableIncompatibleItems(p_item->child(i));
      }
    };
    for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i) {
      disableIncompatibleItems(m_tagTree->topLevelItem(i));
    }

    // Update font for all items
    std::function<void(QTreeWidgetItem *)> updateItemFont = [&](QTreeWidgetItem *p_item) {
      QFont font = p_item->font(Column::Name);
      // set bold when item is selected or enabled
      bool shouldBold = !selectedItems.isEmpty() &&
                        (selectedItems.contains(p_item) || (p_item->flags() & Qt::ItemIsEnabled));
      font.setBold(shouldBold);
      p_item->setFont(Column::Name, font);
      for (int i = 0; i < p_item->childCount(); ++i) {
        updateItemFont(p_item->child(i));
      }
    };
    for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i) {
      updateItemFont(m_tagTree->topLevelItem(i));
    }

    timer->start();
  });
  connect(m_tagTree, &QTreeWidget::customContextMenuRequested, this,
          &TagExplorer::handleTagTreeContextMenuRequested);
  connect(m_tagTree, &TreeWidget::itemMoved, this, &TagExplorer::handleTagMoved);

  m_tagTreeNavigationWrapper.reset(
      new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_tagTree));
  NavigationModeMgr::getInst().registerNavigationTarget(m_tagTreeNavigationWrapper.data());
}

void TagExplorer::setupNodeList(QWidget *p_parent) {
  m_nodeList = new ListWidget(p_parent);
  m_nodeList->setContextMenuPolicy(Qt::CustomContextMenu);
  m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(m_nodeList, &QListWidget::customContextMenuRequested, this,
          &TagExplorer::handleNodeListContextMenuRequested);
  connect(m_nodeList, &QListWidget::itemActivated, this, &TagExplorer::openItem);

  m_nodeListNavigationWrapper.reset(
      new NavigationModeWrapper<QListWidget, QListWidgetItem>(m_nodeList));
  NavigationModeMgr::getInst().registerNavigationTarget(m_nodeListNavigationWrapper.data());
}

QByteArray TagExplorer::saveState() const { return m_splitter->saveState(); }

void TagExplorer::restoreState(const QByteArray &p_data) { m_splitter->restoreState(p_data); }

void TagExplorer::setNotebook(const QSharedPointer<Notebook> &p_notebook) {
  if (m_notebook == p_notebook) {
    return;
  }

  if (m_notebook) {
    disconnect(m_notebook.data(), nullptr, this, nullptr);
  }

  m_notebook = p_notebook;
  if (m_notebook) {
    connect(m_notebook.data(), &Notebook::tagsUpdated, this, &TagExplorer::updateTags);
  }

  m_lastTagName.clear();

  updateTags();
}

void TagExplorer::updateTags() {
  m_tagTree->clear();

  auto tagI = m_notebook ? m_notebook->tag() : nullptr;
  if (!tagI) {
    return;
  }

  const auto &topLevelTags = tagI->getTopLevelTags();
  for (const auto &tag : topLevelTags) {
    auto item = new QTreeWidgetItem(m_tagTree);
    fillTagItem(tag, item);
    loadTagChildren(tag, item);
  }

  m_tagTree->expandAll();

  scrollToTag(m_lastTagName);
}

void TagExplorer::loadTagChildren(const QSharedPointer<Tag> &p_tag, QTreeWidgetItem *p_parentItem) {
  for (const auto &child : p_tag->getChildren()) {
    auto item = new QTreeWidgetItem(p_parentItem);
    fillTagItem(child, item);
    loadTagChildren(child, item);
  }
}

void TagExplorer::fillTagItem(const QSharedPointer<Tag> &p_tag, QTreeWidgetItem *p_item) const {
  p_item->setText(Column::Name, p_tag->name());
  p_item->setToolTip(Column::Name, p_tag->name());
  p_item->setIcon(Column::Name, m_tagIcon);
  p_item->setData(Column::Name, Qt::UserRole, p_tag->name());
  // set no-bold when init
  QFont font = p_item->font(Column::Name);
  font.setBold(false);
  p_item->setFont(Column::Name, font);
}

void TagExplorer::activateTagItem() {
  auto items = m_tagTree->selectedItems();
  if (items.isEmpty()) {
    m_lastTagName.clear();
    m_nodeList->clear();
    return;
  }

  QStringList tags;
  for (const auto &item : items) {
    tags.append(itemTag(item));
  }

  updateNodeList(tags);
}

QString TagExplorer::itemTag(const QTreeWidgetItem *p_item) const {
  return p_item->data(Column::Name, Qt::UserRole).toString();
}

QString TagExplorer::itemNode(const QListWidgetItem *p_item) const {
  return p_item->data(Qt::UserRole).toString();
}

void TagExplorer::updateNodeList(const QStringList &p_tags) {
  m_nodeList->clear();

  Q_ASSERT(m_notebook);
  auto tagI = m_notebook->tag();
  Q_ASSERT(tagI);

  QSet<QString> nodePaths;
  for (const auto &tag : p_tags) {
    const auto paths = tagI->findNodesOfTag(tag);
    if (nodePaths.isEmpty()) {
      nodePaths.unite(QSet<QString>(paths.begin(), paths.end()));
    } else {
      nodePaths.intersect(QSet<QString>(paths.begin(), paths.end()));
    }
  }

  for (const auto &pa : nodePaths) {
    auto node = m_notebook->loadNodeByPath(pa);
    if (!node) {
      qWarning() << "node belongs to tag in DB but not exists" << p_tags.join(", ") << pa;
      continue;
    }

    auto item = new QListWidgetItem(m_nodeList);
    item->setText(node->getName());
    item->setToolTip(NotebookNodeExplorer::generateToolTip(node.data()));
    item->setIcon(m_nodeIcon);
    item->setData(Qt::UserRole, pa);
  }

  VNoteX::getInst().showStatusMessageShort(
      tr("Search of tags succeeded: %1").arg(p_tags.join(", ")));
}

void TagExplorer::handleNodeListContextMenuRequested(const QPoint &p_pos) {
  if (!m_notebook) {
    return;
  }

  auto item = m_nodeList->itemAt(p_pos);
  if (!item) {
    return;
  }

  QMenu menu(this);

  const int selectedCount = m_nodeList->selectedItems().size();

  menu.addAction(tr("&Open"), &menu, [this]() {
    const auto selectedItems = m_nodeList->selectedItems();
    for (const auto &selectedItem : selectedItems) {
      openItem(selectedItem);
    }
  });

  if (selectedCount == 1) {
    menu.addAction(tr("&Locate Node"), &menu, [this]() {
      auto item = m_nodeList->currentItem();
      if (!item) {
        return;
      }

      auto node = m_notebook->loadNodeByPath(itemNode(item));
      Q_ASSERT(node);
      if (node) {
        emit VNoteX::getInst().locateNodeRequested(node.data());
      }
    });
  }

  menu.exec(m_nodeList->mapToGlobal(p_pos));
}

void TagExplorer::openItem(const QListWidgetItem *p_item) {
  if (!p_item) {
    return;
  }

  Q_ASSERT(m_notebook);
  auto node = m_notebook->loadNodeByPath(itemNode(p_item));
  if (node) {
    emit VNoteX::getInst().openNodeRequested(node.data(),
                                             QSharedPointer<FileOpenParameters>::create());
  }
}

void TagExplorer::handleTagTreeContextMenuRequested(const QPoint &p_pos) {
  if (!m_notebook) {
    return;
  }

  QMenu menu(this);

  auto item = m_tagTree->itemAt(p_pos);

  menu.addAction(tr("&New Tag"), this, &TagExplorer::newTag);

  if (item && m_tagTree->selectedItems().size() == 1) {
    menu.addAction(tr("&Rename"), this, &TagExplorer::renameTag);

    menu.addAction(tr("&Delete"), this, &TagExplorer::removeTag);
  }

  menu.exec(m_tagTree->mapToGlobal(p_pos));
}

void TagExplorer::newTag() {
  Q_ASSERT(m_notebook);

  QSharedPointer<Tag> parentTag;

  auto item = m_tagTree->currentItem();
  if (item) {
    const auto tagName = itemTag(item);
    parentTag = m_notebook->tag()->findTag(tagName);
    Q_ASSERT(parentTag);
  }

  NewTagDialog dialog(m_notebook->tag(), parentTag.data(), VNoteX::getInst().getMainWindow());
  dialog.exec();
}

void TagExplorer::renameTag() {
  Q_ASSERT(m_notebook);
  auto item = m_tagTree->currentItem();
  if (!item) {
    return;
  }

  RenameTagDialog dialog(m_notebook->tag(), itemTag(item), VNoteX::getInst().getMainWindow());
  if (dialog.exec() == QDialog::Accepted) {
    scrollToTag(dialog.getTagName());
  }
}

void TagExplorer::removeTag() {
  Q_ASSERT(m_notebook);
  auto item = m_tagTree->currentItem();
  if (!item) {
    return;
  }

  const auto tagName = itemTag(item);
  int okRet = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Warning, tr("Delete the tag and all its chlidren tags?"),
      tr("Only tags and the references of them will be deleted."), QString(),
      VNoteX::getInst().getMainWindow());
  if (okRet != QMessageBox::Ok) {
    return;
  }

  if (m_notebook->tag()->removeTag(tagName)) {
    VNoteX::getInst().showStatusMessageShort(tr("Tag deleted"));
  } else {
    VNoteX::getInst().showStatusMessageShort(tr("Failed to delete tag: %1").arg(tagName));
  }
}

void TagExplorer::handleTagMoved(QTreeWidgetItem *p_item) {
  const auto tagName = itemTag(p_item);
  auto tag = m_notebook->tag()->findTag(tagName);
  Q_ASSERT(tag);
  const auto oldParentName = tag->getParent() ? tag->getParent()->name() : QString();
  const auto newParentName = p_item->parent() ? itemTag(p_item->parent()) : QString();
  if (oldParentName == newParentName) {
    // Sorting tags is not supported for now.
    return;
  }

  qDebug() << "re-parent tag" << tagName << oldParentName << "->" << newParentName;
  bool ret = m_notebook->tag()->moveTag(tagName, newParentName);
  if (!ret) {
    MessageBoxHelper::notify(MessageBoxHelper::Type::Warning,
                             tr("Failed to move tag (%1).").arg(tagName),
                             VNoteX::getInst().getMainWindow());
  }
}

void TagExplorer::scrollToTag(const QString &p_name) {
  if (p_name.isEmpty()) {
    return;
  }

  auto item = TreeWidget::findItem(m_tagTree, p_name, Column::Name);
  if (item) {
    m_tagTree->setCurrentItem(item);
    m_tagTree->scrollToItem(item);
  }
}

void TagExplorer::filterTags(const QString &p_text) {
  std::function<void(QTreeWidgetItem *)> processItem = [&](QTreeWidgetItem *p_item) {
    bool show = p_item->text(Column::Name).contains(p_text, Qt::CaseInsensitive);

    for (int i = 0; i < p_item->childCount(); ++i) {
      processItem(p_item->child(i));
      if (!p_item->child(i)->isHidden()) {
        show = true;
      }
    }

    p_item->setHidden(!show);
  };

  for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i) {
    processItem(m_tagTree->topLevelItem(i));
  }
}
