#include "notebooknodeexplorer.h"

#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QMenu>
#include <QAction>
#include <QSet>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include <notebook/externalnode.h>
#include "exception.h"
#include "messageboxhelper.h"
#include "vnotex.h"
#include "mainwindow.h"
#include <utils/iconutils.h>
#include "treewidget.h"
#include "dialogs/notepropertiesdialog.h"
#include "dialogs/folderpropertiesdialog.h"
#include "dialogs/deleteconfirmdialog.h"
#include "dialogs/sortdialog.h"
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include <utils/clipboardutils.h>
#include "notebookmgr.h"
#include "widgetsfactory.h"
#include "navigationmodemgr.h"

#include <core/fileopenparameters.h>
#include <core/events.h>
#include <core/configmgr.h>

using namespace vnotex;

QIcon NotebookNodeExplorer::s_folderNodeIcon;

QIcon NotebookNodeExplorer::s_fileNodeIcon;

QIcon NotebookNodeExplorer::s_invalidFolderNodeIcon;

QIcon NotebookNodeExplorer::s_invalidFileNodeIcon;

QIcon NotebookNodeExplorer::s_recycleBinNodeIcon;

QIcon NotebookNodeExplorer::s_externalFolderNodeIcon;

QIcon NotebookNodeExplorer::s_externalFileNodeIcon;

NotebookNodeExplorer::NodeData::NodeData()
{
}

NotebookNodeExplorer::NodeData::NodeData(Node *p_node, bool p_loaded)
    : m_type(NodeType::Node),
      m_node(p_node),
      m_loaded(p_loaded)
{
}

NotebookNodeExplorer::NodeData::NodeData(const QSharedPointer<ExternalNode> &p_externalNode)
    : m_type(NodeType::ExternalNode),
      m_externalNode(p_externalNode),
      m_loaded(true)
{
}

NotebookNodeExplorer::NodeData::NodeData(const NodeData &p_other)
{
    m_type = p_other.m_type;
    switch (m_type) {
    case NodeType::Node:
        m_node = p_other.m_node;
        break;

    case NodeType::ExternalNode:
        m_externalNode = p_other.m_externalNode;
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    m_loaded = p_other.m_loaded;
}

NotebookNodeExplorer::NodeData::~NodeData()
{
}

NotebookNodeExplorer::NodeData &NotebookNodeExplorer::NodeData::operator=(const NodeData &p_other)
{
    if (&p_other == this) {
        return *this;
    }

    m_type = p_other.m_type;
    switch (m_type) {
    case NodeType::Node:
        m_node = p_other.m_node;
        break;

    case NodeType::ExternalNode:
        m_externalNode = p_other.m_externalNode;
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    m_loaded = p_other.m_loaded;

    return *this;
}

bool NotebookNodeExplorer::NodeData::isValid() const
{
    return m_type != NodeType::Invalid;
}

bool NotebookNodeExplorer::NodeData::isNode() const
{
    return m_type == NodeType::Node;
}

bool NotebookNodeExplorer::NodeData::isExternalNode() const
{
    return m_type == NodeType::ExternalNode;
}

NotebookNodeExplorer::NodeData::NodeType NotebookNodeExplorer::NodeData::getType() const
{
    return m_type;
}

Node *NotebookNodeExplorer::NodeData::getNode() const
{
    Q_ASSERT(isNode());
    return m_node;
}

ExternalNode *NotebookNodeExplorer::NodeData::getExternalNode() const
{
    Q_ASSERT(isExternalNode());
    return m_externalNode.data();
}

void NotebookNodeExplorer::NodeData::clear()
{
    m_type = NodeType::Invalid;
    m_node = nullptr;
    m_externalNode.clear();
    m_loaded = false;
}

bool NotebookNodeExplorer::NodeData::matched(const Node *p_node) const
{
    if (isNode() && m_node == p_node) {
        return true;
    }

    return false;
}

bool NotebookNodeExplorer::NodeData::isLoaded() const
{
    return m_loaded;
}


NotebookNodeExplorer::NotebookNodeExplorer(QWidget *p_parent)
    : QWidget(p_parent)
{
    initNodeIcons();

    setupUI();
}

void NotebookNodeExplorer::initNodeIcons() const
{
    if (!s_folderNodeIcon.isNull()) {
        return;
    }

    const QString nodeIconFgName = "widgets#notebookexplorer#node_icon#fg";
    const QString invalidNodeIconFgName = "widgets#notebookexplorer#node_icon#invalid#fg";
    const QString externalNodeIconFgName = "widgets#notebookexplorer#external_node_icon#fg";

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(nodeIconFgName);
    const auto invalidFg = themeMgr.paletteColor(invalidNodeIconFgName);
    const auto externalFg = themeMgr.paletteColor(externalNodeIconFgName);

    const QString folderIconName("folder_node.svg");
    const QString fileIconName("file_node.svg");
    const QString recycleBinIconName("recycle_bin.svg");

    s_folderNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), fg);
    s_fileNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), fg);
    s_invalidFolderNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), invalidFg);
    s_invalidFileNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), invalidFg);
    s_recycleBinNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(recycleBinIconName), fg);
    s_externalFolderNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), externalFg);
    s_externalFileNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), externalFg);
}

void NotebookNodeExplorer::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(this);
    mainLayout->addWidget(m_splitter);

    setupMasterExplorer(m_splitter);
    m_splitter->addWidget(m_masterExplorer);

    setFocusProxy(m_masterExplorer);
}

void NotebookNodeExplorer::setupMasterExplorer(QWidget *p_parent)
{
    m_masterExplorer = new TreeWidget(TreeWidget::ClickSpaceToClearSelection, p_parent);
    TreeWidget::setupSingleColumnHeaderlessTree(m_masterExplorer, true, true);
    TreeWidget::showHorizontalScrollbar(m_masterExplorer);

    m_navigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_masterExplorer));
    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationWrapper.data());

    connect(m_masterExplorer, &QTreeWidget::itemExpanded,
            this, [this](QTreeWidgetItem *p_item) {
                auto cnt = p_item->childCount();
                for (int i = 0; i < cnt; ++i) {
                    auto child = p_item->child(i);
                    auto data = getItemNodeData(child);
                    if (data.isNode() && !data.isLoaded()) {
                        loadNode(child, data.getNode(), 1);
                    }
                }
            });

    connect(m_masterExplorer, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &p_pos) {
                if (!m_notebook) {
                    return;
                }

                auto item = m_masterExplorer->itemAt(p_pos);
                auto data = getItemNodeData(item);
                QScopedPointer<QMenu> menu(WidgetsFactory::createMenu());
                if (!data.isValid()) {
                    createContextMenuOnRoot(menu.data());
                } else {
                    if (!allSelectedItemsSameType()) {
                        return;
                    }

                    if (data.isNode()) {
                        createContextMenuOnNode(menu.data(), data.getNode());
                    } else if (data.isExternalNode()) {
                        createContextMenuOnExternalNode(menu.data(), data.getExternalNode());
                    }
                }

                if (!menu->isEmpty()) {
                    menu->exec(m_masterExplorer->mapToGlobal(p_pos));
                }
            });

    connect(m_masterExplorer, &QTreeWidget::itemActivated,
            this, [this](QTreeWidgetItem *p_item, int p_column) {
                Q_UNUSED(p_column);
                auto data = getItemNodeData(p_item);
                if (!data.isValid()) {
                    return;
                }

                if (data.isNode()) {
                    if (checkInvalidNode(data.getNode())) {
                        return;
                    }
                    emit nodeActivated(data.getNode(), QSharedPointer<FileOpenParameters>::create());
                } else if (data.isExternalNode()) {
                    // Import to config first.
                    if (m_autoImportExternalFiles) {
                        auto importedNode = importToIndex(data.getExternalNode());
                        if (importedNode) {
                            emit nodeActivated(importedNode.data(), QSharedPointer<FileOpenParameters>::create());
                        }
                        return;
                    }

                    // Just open it.
                    emit fileActivated(data.getExternalNode()->fetchAbsolutePath(),
                                       QSharedPointer<FileOpenParameters>::create());
                }
            });
}

void NotebookNodeExplorer::setNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    if (p_notebook == m_notebook) {
        return;
    }

    if (m_notebook) {
        disconnect(m_notebook.data(), nullptr, this, nullptr);
    }

    saveNotebookTreeState();

    m_notebook = p_notebook;

    if (m_notebook) {
        connect(m_notebook.data(), &Notebook::nodeUpdated,
                this, [this](const Node *p_node) {
                    updateNode(p_node->getParent());
                });
    }

    generateNodeTree();
}

void NotebookNodeExplorer::clearExplorer()
{
    m_masterExplorer->clear();
}

void NotebookNodeExplorer::generateNodeTree()
{
    clearExplorer();

    if (!m_notebook) {
        return;
    }

    try {
        auto rootNode = m_notebook->getRootNode();

        loadRootNode(rootNode.data());
    } catch (Exception &p_e) {
        QString msg = tr("Failed to load nodes of notebook (%1) (%2).")
                        .arg(m_notebook->getName(), p_e.what());
        qCritical() << msg;
        MessageBoxHelper::notify(MessageBoxHelper::Critical, msg, VNoteX::getInst().getMainWindow());
    }

    // Restore current item.
    auto currentNode = stateCache()->getCurrentItem();

    if (currentNode) {
        setCurrentNode(currentNode);
    } else {
        // Do not focus the recycle bin.
        focusNormalNode();
    }

    stateCache()->clear();
}

void NotebookNodeExplorer::loadRootNode(const Node *p_node) const
{
    Q_ASSERT(p_node->isLoaded() && p_node->isContainer());

    // Render recycle bin node first.
    auto recycleBinNode = m_notebook->getRecycleBinNode();
    if (recycleBinNode) {
        loadRecycleBinNode(recycleBinNode.data());
    }

    // External children.
    if (m_externalFilesVisible) {
        auto externalChildren = p_node->fetchExternalChildren();
        // TODO: Sort external children.
        for (const auto &child : externalChildren) {
            auto item = new QTreeWidgetItem(m_masterExplorer);
            loadNode(item, child);
        }
    }

    // Children.
    auto children = p_node->getChildren();
    sortNodes(children);
    for (const auto &child : children) {
        if (recycleBinNode == child) {
            continue;
        }

        auto item = new QTreeWidgetItem(m_masterExplorer);
        loadNode(item, child.data(), 1);
    }
}

static void clearTreeWigetItemChildren(QTreeWidgetItem *p_item)
{
    auto children = p_item->takeChildren();
    for (auto &child : children) {
        delete child;
    }
}

void NotebookNodeExplorer::loadNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (!p_node->isLoaded()) {
        p_node->load();
    }

    clearTreeWigetItemChildren(p_item);

    fillTreeItem(p_item, p_node, p_level > 0);

    loadChildren(p_item, p_node, p_level - 1);

    if (stateCache()->contains(p_item)) {
        p_item->setExpanded(true);
    }
}

void NotebookNodeExplorer::loadNode(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const
{
    clearTreeWigetItemChildren(p_item);

    fillTreeItem(p_item, p_node);

    // No children for external node.
}

void NotebookNodeExplorer::loadChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (p_level < 0) {
        return;
    }

    // External children.
    if (m_externalFilesVisible && p_node->isContainer()) {
        auto externalChildren = p_node->fetchExternalChildren();
        // TODO: Sort external children.
        for (const auto &child : externalChildren) {
            auto item = new QTreeWidgetItem(p_item);
            loadNode(item, child);
        }
    }

    auto children = p_node->getChildren();
    sortNodes(children);
    for (const auto &child : children) {
        auto item = new QTreeWidgetItem(p_item);
        loadNode(item, child.data(), p_level);
    }
}

void NotebookNodeExplorer::loadRecycleBinNode(Node *p_node) const
{
    if (!m_recycleBinNodeVisible) {
        return;
    }

    auto item = new QTreeWidgetItem();
    item->setWhatsThis(Column::Name,
                       tr("Recycle bin of this notebook. Deleted files could be found here. "
                          "It is organized in folders named by date. Nodes could be moved to "
                          "other folders by Cut and Paste."));
    m_masterExplorer->insertTopLevelItem(0, item);

    loadRecycleBinNode(item, p_node, 1);
}

void NotebookNodeExplorer::loadRecycleBinNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (!m_recycleBinNodeVisible) {
        return;
    }

    if (!p_node->isLoaded()) {
        p_node->load();
    }

    clearTreeWigetItemChildren(p_item);

    setItemNodeData(p_item, NodeData(p_node, true));
    p_item->setText(Column::Name, tr("Recycle Bin"));
    p_item->setIcon(Column::Name, getNodeItemIcon(p_node));

    loadChildren(p_item, p_node, p_level - 1);

    // No need to restore state.
}

void NotebookNodeExplorer::fillTreeItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const
{
    setItemNodeData(p_item, NodeData(p_node, p_loaded));
    p_item->setText(Column::Name, p_node->getName());
    p_item->setIcon(Column::Name, getNodeItemIcon(p_node));
    p_item->setToolTip(Column::Name, p_node->exists() ? p_node->getName() : (tr("[Invalid] %1").arg(p_node->getName())));
}

void NotebookNodeExplorer::fillTreeItem(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const
{
    setItemNodeData(p_item, NodeData(p_node));
    p_item->setText(Column::Name, p_node->getName());
    p_item->setIcon(Column::Name, getNodeItemIcon(p_node.data()));
    p_item->setToolTip(Column::Name, tr("[External] %1").arg(p_node->getName()));
}

const QIcon &NotebookNodeExplorer::getNodeItemIcon(const Node *p_node) const
{
    if (p_node->hasContent()) {
        return p_node->exists() ? s_fileNodeIcon : s_invalidFileNodeIcon;
    } else {
        if (p_node->getUse() == Node::Use::RecycleBin) {
            return s_recycleBinNodeIcon;
        }

        return p_node->exists() ? s_folderNodeIcon : s_invalidFolderNodeIcon;
    }
}

const QIcon &NotebookNodeExplorer::getNodeItemIcon(const ExternalNode *p_node) const
{
    return p_node->isFolder() ? s_externalFolderNodeIcon : s_externalFileNodeIcon;
}

Node *NotebookNodeExplorer::getCurrentNode() const
{
    auto item = m_masterExplorer->currentItem();
    if (item) {
        auto data = getItemNodeData(item);
        while (item && !data.isNode()) {
            item = item->parent();
            if (item) {
                data = getItemNodeData(item);
            } else {
                data.clear();
            }
        }

        if (data.isNode()) {
            return data.getNode();
        }
    }

    return nullptr;
}

void NotebookNodeExplorer::setItemNodeData(QTreeWidgetItem *p_item, const NodeData &p_data)
{
    p_item->setData(Column::Name, Qt::UserRole, QVariant::fromValue(p_data));
}

NotebookNodeExplorer::NodeData NotebookNodeExplorer::getItemNodeData(const QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return NodeData();
    }

    return p_item->data(Column::Name, Qt::UserRole).value<NotebookNodeExplorer::NodeData>();
}

void NotebookNodeExplorer::updateNode(Node *p_node)
{
    if (p_node && p_node->getNotebook() != m_notebook) {
        return;
    }

    auto item = findNode(p_node);
    if (item) {
        bool expanded = item->isExpanded();
        item->setExpanded(false);

        if (m_notebook->isRecycleBinNode(p_node)) {
            loadRecycleBinNode(item, p_node, 1);
        } else {
            loadNode(item, p_node, 1);
        }

        item->setExpanded(expanded);
    } else {
        saveNotebookTreeState(false);

        generateNodeTree();
    }
}

QTreeWidgetItem *NotebookNodeExplorer::findNode(const Node *p_node) const
{
    if (!p_node) {
        return nullptr;
    }

    auto cnt = m_masterExplorer->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        auto item = findNode(m_masterExplorer->topLevelItem(i), p_node);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QTreeWidgetItem *NotebookNodeExplorer::findNode(QTreeWidgetItem *p_item, const Node *p_node) const
{
    auto data = getItemNodeData(p_item);
    if (data.matched(p_node)) {
        return p_item;
    }

    auto cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        auto item = findNode(p_item->child(i), p_node);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QTreeWidgetItem *NotebookNodeExplorer::findNodeChild(QTreeWidgetItem *p_item, const Node *p_node) const
{
    auto cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        auto child = p_item->child(i);
        auto data = getItemNodeData(child);
        if (data.matched(p_node)) {
            return child;
        }
    }

    return nullptr;
}

QTreeWidgetItem *NotebookNodeExplorer::findNodeTopLevelItem(QTreeWidget *p_tree, const Node *p_node) const
{
    auto cnt = p_tree->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        auto child = p_tree->topLevelItem(i);
        auto data = getItemNodeData(child);
        if (data.matched(p_node)) {
            return child;
        }
    }

    return nullptr;
}

void NotebookNodeExplorer::setCurrentNode(Node *p_node)
{
    if (!p_node || !p_node->getParent()) {
        m_masterExplorer->setCurrentItem(nullptr);
        return;
    }

    Q_ASSERT(p_node->getNotebook() == m_notebook);

    // Nodes from root to p_node.
    QList<Node *> nodes;
    auto node = p_node;
    while (node->getParent()) {
        nodes.push_front(node);
        node = node->getParent();
    }

    QList<QTreeWidgetItem *> items;
    auto nodeIt = nodes.constBegin();
    auto item = findNodeTopLevelItem(m_masterExplorer, *nodeIt);
    if (!item) {
        return;
    }
    items.push_back(item);

    ++nodeIt;
    while (nodeIt != nodes.constEnd()) {
        if (!item) {
            return;
        }

        // Find *nodeIt in children of item.
        auto data = getItemNodeData(item);
        Q_ASSERT(data.isNode());
        if (!data.isLoaded()) {
            loadNode(item, data.getNode(), 1);
        }

        auto childItem = findNodeChild(item, *nodeIt);
        if (!childItem) {
            return;
        }
        items.push_back(childItem);

        item = childItem;
        ++nodeIt;
    }

    Q_ASSERT(getItemNodeData(item).getNode() == p_node);

    // Do not expand the last item.
    for (int i = 0; i < items.size() - 1; ++i) {
        items[i]->setExpanded(true);
    }

    m_masterExplorer->setCurrentItem(item);
}

void NotebookNodeExplorer::saveNotebookTreeState(bool p_saveCurrentItem)
{
    if (m_notebook) {
        stateCache()->save(m_masterExplorer, p_saveCurrentItem);
    }
}

QSharedPointer<QTreeWidgetStateCache<Node *>> NotebookNodeExplorer::stateCache() const
{
    Q_ASSERT(m_notebook);
    auto it = m_stateCache.find(m_notebook.data());
    if (it == m_stateCache.end()) {
        auto keyFunc = [](const QTreeWidgetItem *p_item, bool &p_ok) {
                           auto data = NotebookNodeExplorer::getItemNodeData(p_item);
                           if (data.isNode()) {
                               p_ok = true;
                               return data.getNode();
                           }

                           p_ok = false;
                           return static_cast<Node *>(nullptr);
                       };
        auto cache = QSharedPointer<QTreeWidgetStateCache<Node *>>::create(keyFunc);
        it = const_cast<NotebookNodeExplorer *>(this)->m_stateCache.insert(m_notebook.data(), cache);
    }

    return it.value();
}

void NotebookNodeExplorer::clearStateCache(const Notebook *p_notebook)
{
    auto it = m_stateCache.find(p_notebook);
    if (it != m_stateCache.end()) {
        it.value()->clear();
    }
}

void NotebookNodeExplorer::createContextMenuOnRoot(QMenu *p_menu)
{
    auto act = createAction(Action::NewNote, p_menu);
    p_menu->addAction(act);

    act = createAction(Action::NewFolder, p_menu);
    p_menu->addAction(act);

    if (isPasteOnNodeAvailable(nullptr)) {
        p_menu->addSeparator();
        act = createAction(Action::Paste, p_menu);
        p_menu->addAction(act);
    }

    p_menu->addSeparator();

    act = createAction(Action::Reload, p_menu);
    p_menu->addAction(act);

    act = createAction(Action::OpenLocation, p_menu);
    p_menu->addAction(act);
}

void NotebookNodeExplorer::createContextMenuOnNode(QMenu *p_menu, const Node *p_node)
{
    const int selectedSize = m_masterExplorer->selectedItems().size();
    QAction *act = nullptr;

    if (m_notebook->isRecycleBinNode(p_node)) {
        // Recycle bin node.
        act = createAction(Action::Reload, p_menu);
        p_menu->addAction(act);

        if (selectedSize == 1) {
            act = createAction(Action::EmptyRecycleBin, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::OpenLocation, p_menu);
            p_menu->addAction(act);
        }
    } else if (m_notebook->isNodeInRecycleBin(p_node)) {
        // Node in recycle bin.
        act = createAction(Action::Open, p_menu);
        p_menu->addAction(act);

        p_menu->addSeparator();

        act = createAction(Action::Cut, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::DeleteFromRecycleBin, p_menu);
        p_menu->addAction(act);

        p_menu->addSeparator();

        act = createAction(Action::Reload, p_menu);
        p_menu->addAction(act);

        if (selectedSize == 1) {
            p_menu->addSeparator();

            act = createAction(Action::CopyPath, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::OpenLocation, p_menu);
            p_menu->addAction(act);
        }
    } else {
        act = createAction(Action::Open, p_menu);
        p_menu->addAction(act);

        p_menu->addSeparator();

        act = createAction(Action::NewNote, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::NewFolder, p_menu);
        p_menu->addAction(act);

        p_menu->addSeparator();

        act = createAction(Action::Copy, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::Cut, p_menu);
        p_menu->addAction(act);

        if (selectedSize == 1 && isPasteOnNodeAvailable(p_node)) {
            act = createAction(Action::Paste, p_menu);
            p_menu->addAction(act);
        }

        act = createAction(Action::Delete, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::RemoveFromConfig, p_menu);
        p_menu->addAction(act);

        p_menu->addSeparator();

        act = createAction(Action::Reload, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::Sort, p_menu);
        p_menu->addAction(act);

        if (selectedSize == 1) {
            p_menu->addSeparator();

            act = createAction(Action::CopyPath, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::OpenLocation, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::Properties, p_menu);
            p_menu->addAction(act);
        }
    }
}

void NotebookNodeExplorer::createContextMenuOnExternalNode(QMenu *p_menu, const ExternalNode *p_node)
{
    Q_UNUSED(p_node);

    const int selectedSize = m_masterExplorer->selectedItems().size();
    QAction *act = nullptr;

    act = createAction(Action::Open, p_menu);
    p_menu->addAction(act);

    act = createAction(Action::ImportToConfig, p_menu);
    p_menu->addAction(act);

    if (selectedSize == 1) {
        p_menu->addSeparator();

        act = createAction(Action::CopyPath, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::OpenLocation, p_menu);
        p_menu->addAction(act);
    }
}

static QIcon generateMenuActionIcon(const QString &p_name)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    return IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(p_name));
}

QAction *NotebookNodeExplorer::createAction(Action p_act, QObject *p_parent)
{
    QAction *act = nullptr;
    switch (p_act) {
    case Action::NewNote:
        act = new QAction(generateMenuActionIcon("new_note.svg"),
                          tr("New N&ote"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, []() {
                    emit VNoteX::getInst().newNoteRequested();
                });
        break;

    case Action::NewFolder:
        act = new QAction(generateMenuActionIcon("new_folder.svg"),
                          tr("New &Folder"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, []() {
                    emit VNoteX::getInst().newFolderRequested();
                });
        break;

    case Action::Properties:
        act = new QAction(generateMenuActionIcon("properties.svg"),
                          tr("&Properties"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto node = getCurrentNode();
                    if (checkInvalidNode(node)) {
                        return;
                    }

                    int ret = QDialog::Rejected;
                    if (node->hasContent()) {
                        NotePropertiesDialog dialog(node, VNoteX::getInst().getMainWindow());
                        ret = dialog.exec();
                    } else {
                        FolderPropertiesDialog dialog(node, VNoteX::getInst().getMainWindow());
                        ret = dialog.exec();
                    }

                    if (ret == QDialog::Accepted) {
                        setCurrentNode(node);
                    }
                });
        break;

    case Action::OpenLocation:
        act = new QAction(tr("Open &Location"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto item = m_masterExplorer->currentItem();
                    if (!item) {
                        if (m_notebook) {
                            auto locationPath = m_notebook->getRootFolderAbsolutePath();
                            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(locationPath));
                        }
                        return;
                    }
                    auto data = getItemNodeData(item);
                    QString locationPath;
                    if (data.isNode()) {
                        auto node = data.getNode();
                        if (checkInvalidNode(node)) {
                            return;
                        }

                        locationPath = node->fetchAbsolutePath();
                        if (!node->isContainer()) {
                            locationPath = PathUtils::parentDirPath(locationPath);
                        }
                    } else if (data.isExternalNode()) {
                        auto externalNode = data.getExternalNode();
                        locationPath = externalNode->fetchAbsolutePath();
                        if (!externalNode->isFolder()) {
                            locationPath = PathUtils::parentDirPath(locationPath);
                        }
                    }

                    if (!locationPath.isEmpty()) {
                        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(locationPath));
                    }
                });
        break;

    case Action::CopyPath:
        act = new QAction(tr("Cop&y Path"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto item = m_masterExplorer->currentItem();
                    if (!item) {
                        return;
                    }
                    auto data = getItemNodeData(item);
                    QString nodePath;
                    if (data.isNode()) {
                        auto node = data.getNode();
                        if (checkInvalidNode(node)) {
                            return;
                        }
                        nodePath = node->fetchAbsolutePath();
                    } else if (data.isExternalNode()) {
                        nodePath = data.getExternalNode()->fetchAbsolutePath();
                    }
                    if (!nodePath.isEmpty()) {
                        ClipboardUtils::setTextToClipboard(nodePath);
                        VNoteX::getInst().showStatusMessageShort(tr("Copied path: %1").arg(nodePath));
                    }
                });
        break;

    case Action::Copy:
        act = new QAction(tr("&Copy"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    copySelectedNodes(false);
                });
        break;

    case Action::Cut:
        act = new QAction(tr("C&ut"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    copySelectedNodes(true);
                });
        break;

    case Action::Paste:
        act = new QAction(tr("&Paste"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    pasteNodesFromClipboard();
                });
        break;

    case Action::EmptyRecycleBin:
        act = new QAction(tr("&Empty"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto rbNode = m_notebook->getRecycleBinNode().data();
                    auto rbNodePath = rbNode->fetchAbsolutePath();
                    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Warning,
                                                                 tr("Empty the recycle bin of this notebook?"),
                                                                 tr("All files in recycle bin will be deleted permanently."),
                                                                 tr("Location of recycle bin: %1").arg(rbNodePath));
                    if (ret != QMessageBox::Ok) {
                        return;
                    }

                    try {
                        m_notebook->emptyNode(rbNode, true);
                    } catch (Exception &p_e) {
                        MessageBoxHelper::notify(MessageBoxHelper::Critical,
                                                 tr("Failed to empty recycle bin (%1) (%2).").arg(rbNodePath, p_e.what()),
                                                 VNoteX::getInst().getMainWindow());
                    }

                    updateNode(rbNode);
                });
        break;

    case Action::Delete:
        act = new QAction(tr("&Delete"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    removeSelectedNodes(false);
                });
        break;

    case Action::DeleteFromRecycleBin:
        // It is fine to have &D with Action::Delete since they won't be at the same context.
        act = new QAction(tr("&Delete From Recycle Bin"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    removeSelectedNodes(true);
                });
        break;

    case Action::RemoveFromConfig:
        act = new QAction(tr("&Remove From Index"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::removeSelectedNodesFromConfig);
        break;

    case Action::Sort:
        act = new QAction(generateMenuActionIcon("sort.svg"), tr("&Sort"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::manualSort);
        break;

    case Action::Reload:
        act = new QAction(tr("Re&load"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto node = currentExploredFolderNode();
                    if (m_notebook && node) {
                        // TODO: emit signals to notify other components.
                        m_notebook->reloadNode(node);
                    }
                    updateNode(node);
                });
        break;

    case Action::ImportToConfig:
        act = new QAction(tr("&Import To Index"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto nodes = getSelectedNodes().second;
                    importToIndex(nodes);
                });
        break;

    case Action::Open:
        act = new QAction(tr("&Open"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::openSelectedNodes);
        break;
    }

    return act;
}

void NotebookNodeExplorer::copySelectedNodes(bool p_move)
{
    auto nodes = getSelectedNodes().first;
    if (nodes.isEmpty()) {
        return;
    }

    filterAwayChildrenNodes(nodes);

    ClipboardData cdata(VNoteX::getInst().getInstanceId(),
                        p_move ? ClipboardData::MoveNode : ClipboardData::CopyNode);
    for (auto node : nodes) {
        if (checkInvalidNode(node)) {
            continue;
        }

        auto item = QSharedPointer<NodeClipboardDataItem>::create(node->getNotebook()->getId(),
                                                                  node->fetchPath());
        cdata.addItem(item);
    }

    auto text = cdata.toJsonText();
    ClipboardUtils::setTextToClipboard(text);

    size_t nrItems = cdata.getData().size();
    VNoteX::getInst().showStatusMessageShort(tr("Copied %n item(s)", "", static_cast<int>(nrItems)));
}

QPair<QVector<Node *>, QVector<ExternalNode *>> NotebookNodeExplorer::getSelectedNodes() const
{
    QPair<QVector<Node *>, QVector<ExternalNode *>> nodes;

    auto items = m_masterExplorer->selectedItems();
    for (auto &item : items) {
        auto data = getItemNodeData(item);
        if (data.isNode()) {
            nodes.first.push_back(data.getNode());
        } else if (data.isExternalNode()) {
            nodes.second.push_back(data.getExternalNode());
        }
    }

    return nodes;
}

QSharedPointer<ClipboardData> NotebookNodeExplorer::tryFetchClipboardData()
{
    auto text = ClipboardUtils::getTextFromClipboard();
    return ClipboardData::fromJsonText(text);
}

static bool isValidClipboardData(const ClipboardData *p_data)
{
    if (!p_data) {
        return false;
    }

    if (p_data->getInstanceId() != VNoteX::getInst().getInstanceId()) {
        return false;
    }

    if (p_data->getData().isEmpty()) {
        return false;
    }

    auto act = p_data->getAction();
    if (act != ClipboardData::CopyNode && act != ClipboardData::MoveNode) {
        return false;
    }

    return true;
}

bool NotebookNodeExplorer::isPasteOnNodeAvailable(const Node *p_node) const
{
    Q_UNUSED(p_node);
    auto cdata = tryFetchClipboardData();
    return isValidClipboardData(cdata.data());
}

static QSharedPointer<Node> getNodeFromClipboardDataItem(const NodeClipboardDataItem *p_item)
{
    Q_ASSERT(p_item);
    auto notebook = VNoteX::getInst().getNotebookMgr().findNotebookById(p_item->m_notebookId);
    if (!notebook) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to find notebook by ID (%1)").arg(p_item->m_notebookId));
        return nullptr;
    }

    auto node = notebook->loadNodeByPath(p_item->m_nodeRelativePath);
    Q_ASSERT(!node || node->fetchPath() == p_item->m_nodeRelativePath);
    return node;
}

void NotebookNodeExplorer::pasteNodesFromClipboard()
{
    // Identify the dest node.
    auto destNode = getCurrentNode();
    if (!destNode) {
        destNode = m_notebook->getRootNode().data();
    } else {
        // Current node may be a file node.
        if (!destNode->isContainer()) {
            destNode = destNode->getParent();
        } else if (checkInvalidNode(destNode)) {
            return;
        }
    }

    Q_ASSERT(destNode && destNode->isContainer());

    // Fetch source nodes from clipboard.
    auto cdata = tryFetchClipboardData();
    if (!isValidClipboardData(cdata.data())) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Invalid clipboard data to paste."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    QVector<QSharedPointer<Node>> srcNodes;
    auto items = cdata->getData();
    for (auto &item : items) {
        auto nodeItem = dynamic_cast<NodeClipboardDataItem *>(item.data());
        Q_ASSERT(nodeItem);
        auto src = getNodeFromClipboardDataItem(nodeItem);
        if (!src) {
            continue;
        } else if (src == destNode) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     tr("Destination is detected in sources (%1). Operation is cancelled.")
                                        .arg(destNode->fetchAbsolutePath()),
                                      VNoteX::getInst().getMainWindow());
            return;
        }

        srcNodes.push_back(src);
    }

    bool isMove = cdata->getAction() == ClipboardData::MoveNode;
    QVector<const Node *> pastedNodes;
    QSet<Node *> nodesNeedUpdate;
    for (auto srcNode : srcNodes) {
        Q_ASSERT(srcNode->exists());

        if (isMove) {
            // Notice the view area to close any opened view windows.
            auto event = QSharedPointer<Event>::create();
            emit nodeAboutToMove(srcNode.data(), event);
            if (!event->m_response.toBool()) {
                continue;
            }
        }

        auto srcPath = srcNode->fetchAbsolutePath();
        auto srcParentNode = srcNode->getParent();

        try {
            auto notebook = destNode->getNotebook();
            auto pastedNode = notebook->copyNodeAsChildOf(srcNode, destNode, isMove);
            pastedNodes.push_back(pastedNode.data());
        } catch (Exception &p_e) {
            MessageBoxHelper::notify(MessageBoxHelper::Critical,
                                     tr("Failed to copy source (%1) to destination (%2) (%3).")
                                       .arg(srcPath, destNode->fetchAbsolutePath(), p_e.what()),
                                     VNoteX::getInst().getMainWindow());
        }

        if (isMove) {
            nodesNeedUpdate.insert(srcParentNode);
        }
    }

    for (auto node : nodesNeedUpdate) {
        updateNode(node);

        // Deleted src nodes may be the current node in cache. Clear the cache.
        clearStateCache(node->getNotebook());
    }

    // Update and expand dest node. Select all pasted nodes.
    updateAndExpandNode(destNode);
    selectNodes(pastedNodes);

    if (isMove) {
        ClipboardUtils::clearClipboard();
    }

    VNoteX::getInst().showStatusMessageShort(tr("Pasted %n item(s)", "", pastedNodes.size()));
}

void NotebookNodeExplorer::setNodeExpanded(const Node *p_node, bool p_expanded)
{
    auto item = findNode(p_node);
    if (item) {
        item->setExpanded(p_expanded);
    }
}

void NotebookNodeExplorer::selectNodes(const QVector<const Node *> &p_nodes)
{
    bool firstItem = true;
    for (auto node : p_nodes) {
        auto item = findNode(node);
        if (item) {
            auto flags = firstItem ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select;
            m_masterExplorer->setCurrentItem(item, 0, flags);
            firstItem = false;
        }
    }
}

void NotebookNodeExplorer::removeSelectedNodes(bool p_skipRecycleBin)
{
    QString text;
    QString info;
    if (p_skipRecycleBin) {
        text = tr("Delete these folders and notes permanently?");
        info = tr("Files will be deleted permanently and could not be found even "
                  "in operating system's recycle bin.");
    } else {
        text = tr("Delete these folders and notes?");
        info = tr("Deleted files could be found in the recycle bin of notebook.");
    }

    auto nodes = confirmSelectedNodes(tr("Confirm Deletion"), text, info);
    removeNodes(nodes, p_skipRecycleBin, false);
}

QVector<Node *> NotebookNodeExplorer::confirmSelectedNodes(const QString &p_title,
                                                           const QString &p_text,
                                                           const QString &p_info) const
{
    auto nodes = getSelectedNodes().first;
    if (nodes.isEmpty()) {
        return nodes;
    }

    QVector<ConfirmItemInfo> items;
    for (const auto &node : nodes) {
        items.push_back(ConfirmItemInfo(getNodeItemIcon(node),
                                        node->getName(),
                                        node->fetchAbsolutePath(),
                                        node->fetchAbsolutePath(),
                                        (void *)node));
    }

    DeleteConfirmDialog dialog(p_title,
                               p_text,
                               p_info,
                               items,
                               DeleteConfirmDialog::Flag::None,
                               false,
                               VNoteX::getInst().getMainWindow());

    QVector<Node *> nodesToDelete;
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();
        for (const auto &item : items) {
            nodesToDelete.push_back(static_cast<Node *>(item.m_data));
        }
    }

    return nodesToDelete;
}

void NotebookNodeExplorer::removeNodes(QVector<Node *> p_nodes,
                                       bool p_skipRecycleBin,
                                       bool p_configOnly)
{
    if (p_nodes.isEmpty()) {
        return;
    }

    filterAwayChildrenNodes(p_nodes);

    int nrDeleted = 0;
    QSet<Node *> nodesNeedUpdate;
    for (auto node : p_nodes) {
        auto srcName = node->getName();
        auto srcPath = node->fetchAbsolutePath();
        auto srcParentNode = node->getParent();
        try {
            auto event = QSharedPointer<Event>::create();
            emit nodeAboutToRemove(node, event);
            if (!event->m_response.toBool()) {
                continue;
            }

            if (p_configOnly || p_skipRecycleBin) {
                m_notebook->removeNode(node, false, p_configOnly);
            } else {
                m_notebook->moveNodeToRecycleBin(node);
            }

            ++nrDeleted;
        } catch (Exception &p_e) {
            MessageBoxHelper::notify(MessageBoxHelper::Critical,
                                     tr("Failed to delete/remove item (%1) (%2) (%3).")
                                       .arg(srcName, srcPath, p_e.what()),
                                     VNoteX::getInst().getMainWindow());
        }

        nodesNeedUpdate.insert(srcParentNode);
    }

    for (auto node : nodesNeedUpdate) {
        updateNode(node);
    }

    if (!p_configOnly && !p_skipRecycleBin && m_recycleBinNodeVisible) {
        updateNode(m_notebook->getRecycleBinNode().data());
    }

    VNoteX::getInst().showStatusMessageShort(tr("Deleted/Removed %n item(s)", "", nrDeleted));
}

void NotebookNodeExplorer::removeSelectedNodesFromConfig()
{
    auto nodes = confirmSelectedNodes(tr("Confirm Removal"),
                                      tr("Remove these folders and notes from index?"),
                                      tr("Files are not touched but just removed from notebook index."));
    removeNodes(nodes, false, true);
}

void NotebookNodeExplorer::filterAwayChildrenNodes(QVector<Node *> &p_nodes)
{
    for (int i = p_nodes.size() - 1; i >= 0; --i) {
        // Check if j is i's ancestor.
        for (int j = p_nodes.size() - 1; j >= 0; --j) {
            if (i == j) {
                continue;
            }

            if (Node::isAncestor(p_nodes[j], p_nodes[i])) {
                p_nodes.remove(i);
                break;
            }
        }
    }
}

void NotebookNodeExplorer::updateAndExpandNode(Node *p_node)
{
    setNodeExpanded(p_node, false);
    updateNode(p_node);
    setNodeExpanded(p_node, true);
}

bool NotebookNodeExplorer::allSelectedItemsSameType() const
{
    auto items = m_masterExplorer->selectedItems();
    if (items.size() < 2) {
        return true;
    }

    auto type = getItemNodeData(items.first()).getType();
    for (int i = 1; i < items.size(); ++i) {
        auto itype = getItemNodeData(items[i]).getType();
        if (itype != type) {
            return false;
        }
    }

    if (type == NodeData::NodeType::Node) {
        bool hasNormalNode = false;
        bool hasNodeInRecycleBin = false;
        for (auto &item : items) {
            auto node = getItemNodeData(item).getNode();
            if (m_notebook->isRecycleBinNode(node)) {
                return false;
            } else if (m_notebook->isNodeInRecycleBin(node)) {
                if (hasNormalNode) {
                    return false;
                }

                hasNodeInRecycleBin = true;
            } else {
                if (hasNodeInRecycleBin) {
                    return false;
                }

                hasNormalNode = true;
            }
        }
    }

    return true;
}

void NotebookNodeExplorer::reload()
{
    updateNode(nullptr);
}

void NotebookNodeExplorer::focusNormalNode()
{
    auto item = m_masterExplorer->currentItem();
    if (item && (!m_recycleBinNodeVisible || item != m_masterExplorer->topLevelItem(0))) {
        // Not recycle bin.
        return;
    }

    m_masterExplorer->setCurrentItem(m_masterExplorer->topLevelItem(m_recycleBinNodeVisible ? 1 : 0));
}

void NotebookNodeExplorer::sortNodes(QVector<QSharedPointer<Node>> &p_nodes) const
{
    if (m_viewOrder == ViewOrder::OrderedByConfiguration) {
        return;
    }

    // Put containers first.
    int firstFileIndex = p_nodes.size();
    for (int i = 0; i < p_nodes.size(); ++i) {
        if (p_nodes[i]->isContainer()) {
            // If it is a container, load it to set its created time and modified time.
            p_nodes[i]->load();
        } else {
            firstFileIndex = i;
            break;
        }
    }

    // Sort containers.
    sortNodes(p_nodes, 0, firstFileIndex, m_viewOrder);

    // Sort non-containers.
    sortNodes(p_nodes, firstFileIndex, p_nodes.size(), m_viewOrder);
}

void NotebookNodeExplorer::sortNodes(QVector<QSharedPointer<Node>> &p_nodes, int p_start, int p_end, int p_viewOrder) const
{
    if (p_start >= p_end) {
        return;
    }

    bool reversed = false;
    switch (p_viewOrder) {
    case ViewOrder::OrderedByNameReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByName:
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> p_b) {
            if (reversed) {
                return p_b->getName() < p_a->getName();
            } else {
                return p_a->getName() < p_b->getName();
            }
        });
        break;

    case ViewOrder::OrderedByCreatedTimeReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByCreatedTime:
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> p_b) {
            if (reversed) {
                return p_b->getCreatedTimeUtc() < p_a->getCreatedTimeUtc();
            } else {
                return p_a->getCreatedTimeUtc() < p_b->getCreatedTimeUtc();
            }
        });
        break;

    case ViewOrder::OrderedByModifiedTimeReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByModifiedTime:
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> p_b) {
            if (reversed) {
                return p_b->getModifiedTimeUtc() < p_a->getModifiedTimeUtc();
            } else {
                return p_a->getModifiedTimeUtc() < p_b->getModifiedTimeUtc();
            }
        });
        break;

    default:
        break;
    }
}

void NotebookNodeExplorer::setRecycleBinNodeVisible(bool p_visible)
{
    if (m_recycleBinNodeVisible == p_visible) {
        return;
    }

    m_recycleBinNodeVisible = p_visible;
    reload();
}

void NotebookNodeExplorer::setExternalFilesVisible(bool p_visible)
{
    if (m_externalFilesVisible == p_visible) {
        return;
    }

    m_externalFilesVisible = p_visible;
    reload();
}

void NotebookNodeExplorer::setAutoImportExternalFiles(bool p_enabled)
{
    if (m_autoImportExternalFiles == p_enabled) {
        return;
    }

    m_autoImportExternalFiles = p_enabled;
}

void NotebookNodeExplorer::manualSort()
{
    auto node = getCurrentNode();
    if (!node) {
        return;
    }

    auto parentNode = node->getParent();
    bool isNotebook = parentNode->isRoot();

    // Check whether sort files or folders based on current node type.
    bool sortFolders = node->isContainer();

    SortDialog sortDlg(sortFolders ? tr("Sort Folders") : tr("Sort Notes"),
                       tr("Sort nodes under %1 (%2) in the configuration file.").arg(
                           isNotebook ? tr("notebook") : tr("folder"),
                           isNotebook ? m_notebook->getName() : parentNode->getName()),
                       VNoteX::getInst().getMainWindow());

    QVector<int> selectedIdx;

    // Update the tree.
    {
        auto treeWidget = sortDlg.getTreeWidget();
        treeWidget->clear();
        treeWidget->setColumnCount(2);
        treeWidget->setHeaderLabels({tr("Name"), tr("Created Time"), tr("Modified Time")});

        const auto &children = parentNode->getChildrenRef();
        for (int i = 0; i < children.size(); ++i) {
            const auto &child = children[i];
            if (m_notebook->isRecycleBinNode(child.data())) {
                continue;
            }

            bool selected = sortFolders ? child->isContainer() : !child->isContainer();
            if (selected) {
                selectedIdx.push_back(i);

                QStringList cols {child->getName(),
                                  Utils::dateTimeString(child->getCreatedTimeUtc().toLocalTime()),
                                  Utils::dateTimeString(child->getModifiedTimeUtc().toLocalTime())};
                auto item = new QTreeWidgetItem(treeWidget, cols);
                item->setData(0, Qt::UserRole, i);
            }
        }

        sortDlg.updateTreeWidget();
    }

    if (sortDlg.exec() == QDialog::Accepted) {
        const auto data = sortDlg.getSortedData();
        Q_ASSERT(data.size() == selectedIdx.size());
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }
        parentNode->sortChildren(selectedIdx, sortedIdx);
        updateNode(parentNode);
    }
}

Node *NotebookNodeExplorer::currentExploredFolderNode() const
{
    if (!m_notebook) {
        return nullptr;
    }

    auto node = getCurrentNode();
    if (node) {
        if (!node->isContainer()) {
            node = node->getParent();
        }
        Q_ASSERT(node && node->isContainer());
    } else {
        node = m_notebook->getRootNode().data();
    }

    return node;
}

void NotebookNodeExplorer::setViewOrder(int p_order)
{
    if (m_viewOrder == p_order) {
        return;
    }

    m_viewOrder = p_order;
    reload();
}

void NotebookNodeExplorer::openSelectedNodes()
{
    // Support nodes and external nodes.
    // Do nothing for folders.
    auto selectedNodes = getSelectedNodes();
    for (const auto &externalNode : selectedNodes.second) {
        if (!externalNode->isFolder()) {
            emit fileActivated(externalNode->fetchAbsolutePath(), QSharedPointer<FileOpenParameters>::create());
        }
    }

    for (const auto &node : selectedNodes.first) {
        if (checkInvalidNode(node)) {
            continue;
        }

        if (node->hasContent()) {
            emit nodeActivated(node, QSharedPointer<FileOpenParameters>::create());
        }
    }
}

QSharedPointer<Node> NotebookNodeExplorer::importToIndex(const ExternalNode *p_node)
{
    auto node = m_notebook->addAsNode(p_node->getNode(),
                                      p_node->isFolder() ? Node::Flag::Container : Node::Flag::Content,
                                      p_node->getName(),
                                      NodeParameters());
    updateNode(p_node->getNode());
    if (node) {
        setCurrentNode(node.data());
    }
    return node;
}

void NotebookNodeExplorer::importToIndex(const QVector<ExternalNode *> &p_nodes)
{
    QSet<Node *> nodesToUpdate;
    Node *currentNode = nullptr;

    for (auto externalNode : p_nodes) {
        auto node = m_notebook->addAsNode(externalNode->getNode(),
                                          externalNode->isFolder() ? Node::Flag::Container : Node::Flag::Content,
                                          externalNode->getName(),
                                          NodeParameters());
        nodesToUpdate.insert(externalNode->getNode());
        currentNode = node.data();
    }

    for (auto node : nodesToUpdate) {
        updateNode(node);
    }
    if (currentNode) {
        setCurrentNode(currentNode);
    }
}

bool NotebookNodeExplorer::checkInvalidNode(const Node *p_node) const
{
    if (!p_node) {
        return true;
    }

    if (!p_node->exists()) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Invalid node (%1).").arg(p_node->getName()),
                                 tr("Please check if the node exists on the disk."),
                                 p_node->fetchAbsolutePath(),
                                 VNoteX::getInst().getMainWindow());
        return true;
    }

    return false;
}
