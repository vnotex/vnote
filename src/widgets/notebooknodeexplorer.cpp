#include "notebooknodeexplorer.h"

#include <QVBoxLayout>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QShortcut>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include <notebook/externalnode.h>
#include <notebook/nodeparameters.h>
#include <core/exception.h>
#include "messageboxhelper.h"
#include "vnotex.h"
#include "mainwindow.h"
#include <utils/iconutils.h>
#include <utils/docsutils.h>
#include "treewidget.h"
#include "listwidget.h"
#include "dialogs/notepropertiesdialog.h"
#include "dialogs/folderpropertiesdialog.h"
#include "dialogs/deleteconfirmdialog.h"
#include "dialogs/sortdialog.h"
#include "dialogs/viewtagsdialog.h"
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include <utils/clipboardutils.h>
#include "notebookmgr.h"
#include "widgetsfactory.h"
#include "navigationmodemgr.h"

#include <core/fileopenparameters.h>
#include <core/events.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <buffer/filetypehelper.h>

using namespace vnotex;

QIcon NotebookNodeExplorer::s_nodeIcons[NodeIcon::MaxIcons];

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

const QSharedPointer<ExternalNode> &NotebookNodeExplorer::NodeData::getExternalNode() const
{
    Q_ASSERT(isExternalNode());
    return m_externalNode;
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

bool NotebookNodeExplorer::NodeData::matched(const QString &p_name) const
{
    if (isNode()) {
        return m_node->getName() == p_name;
    } else {
        return m_externalNode->getName() == p_name;
    }
}

bool NotebookNodeExplorer::NodeData::isLoaded() const
{
    return m_loaded;
}


void NotebookNodeExplorer::CacheData::clear()
{
    if (m_masterStateCache) {
        m_masterStateCache->clear();
    }
    m_currentSlaveName.clear();
}


NotebookNodeExplorer::NotebookNodeExplorer(QWidget *p_parent)
    : QWidget(p_parent)
{
    initNodeIcons();

    setupUI();

    setupShortcuts();
}

void NotebookNodeExplorer::initNodeIcons() const
{
    if (!s_nodeIcons[0].isNull()) {
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

    s_nodeIcons[NodeIcon::FolderNode] = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), fg);
    s_nodeIcons[NodeIcon::FileNode] = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), fg);
    s_nodeIcons[NodeIcon::InvalidFolderNode] = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), invalidFg);
    s_nodeIcons[NodeIcon::InvalidFileNode] = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), invalidFg);
    s_nodeIcons[NodeIcon::ExternalFolderNode] = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), externalFg);
    s_nodeIcons[NodeIcon::ExternalFileNode] = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), externalFg);
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

    m_masterNavigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_masterExplorer));
    NavigationModeMgr::getInst().registerNavigationTarget(m_masterNavigationWrapper.data());

    connect(m_masterExplorer, &QTreeWidget::itemExpanded,
            this, &NotebookNodeExplorer::loadMasterItemChildren);

    connect(m_masterExplorer, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                if (!m_notebook) {
                    return;
                }

                auto item = m_masterExplorer->itemAt(pos);
                QScopedPointer<QMenu> menu(WidgetsFactory::createMenu());
                if (!item) {
                    createMasterContextMenuOnRoot(menu.data());
                } else {
                    if (!isMasterAllSelectedItemsSameType()) {
                        return;
                    }

                    auto data = getItemNodeData(item);
                    if (data.isNode()) {
                        createContextMenuOnNode(menu.data(), data.getNode(), true);
                    } else if (data.isExternalNode()) {
                        createContextMenuOnExternalNode(menu.data(), data.getExternalNode().data(), true);
                    }
                }

                if (!menu->isEmpty()) {
                    menu->exec(m_masterExplorer->mapToGlobal(pos));
                }
            });

    connect(m_masterExplorer, &QTreeWidget::itemActivated,
            this, [this](QTreeWidgetItem *p_item, int p_column) {
                Q_UNUSED(p_column);
                if (!isCombinedExploreMode()) {
                    return;
                }
                auto data = getItemNodeData(p_item);
                activateItemNode(data);
            });
}

void NotebookNodeExplorer::activateItemNode(const NodeData &p_data)
{
    if (!p_data.isValid()) {
        return;
    }

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto defaultMode = coreConfig.getDefaultOpenMode();

    if (p_data.isNode()) {
        if (checkInvalidNode(p_data.getNode())) {
            return;
        }
        auto paras = QSharedPointer<FileOpenParameters>::create();
        paras->m_mode = defaultMode;
        emit nodeActivated(p_data.getNode(), paras);
    } else if (p_data.isExternalNode()) {
        // Import to config first.
        if (m_autoImportExternalFiles) {
            auto importedNode = importToIndex(p_data.getExternalNode());
            if (importedNode) {
                auto paras = QSharedPointer<FileOpenParameters>::create();
                paras->m_mode = defaultMode;
                emit nodeActivated(importedNode.data(), paras);
            }
            return;
        }

        // Just open it.
        auto paras = QSharedPointer<FileOpenParameters>::create();
        paras->m_mode = defaultMode;
        emit fileActivated(p_data.getExternalNode()->fetchAbsolutePath(), paras);
    }
}

void NotebookNodeExplorer::setupSlaveExplorer()
{
    Q_ASSERT(!m_slaveExplorer);
    m_slaveExplorer = new ListWidget(m_splitter);
    m_splitter->addWidget(m_slaveExplorer);

    m_slaveExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
    m_slaveExplorer->setSelectionMode(QAbstractItemView::ExtendedSelection);

    connect(m_slaveExplorer, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                Q_ASSERT(!isCombinedExploreMode());
                if (!m_notebook) {
                    return;
                }

                auto item = m_slaveExplorer->itemAt(pos);
                QScopedPointer<QMenu> menu(WidgetsFactory::createMenu());
                if (!item) {
                    createSlaveContextMenuOnMasterNode(menu.data());
                } else {
                    if (!isSlaveAllSelectedItemsSameType()) {
                        return;
                    }

                    auto data = getItemNodeData(item);
                    if (data.isNode()) {
                        createContextMenuOnNode(menu.data(), data.getNode(), false);
                    } else if (data.isExternalNode()) {
                        createContextMenuOnExternalNode(menu.data(), data.getExternalNode().data(), false);
                    }
                }

                if (!menu->isEmpty()) {
                    menu->exec(m_slaveExplorer->mapToGlobal(pos));
                }
            });
    connect(m_slaveExplorer, &QListWidget::itemActivated,
            this, [this](QListWidgetItem *p_item) {
                Q_ASSERT(!isCombinedExploreMode());
                auto data = getItemNodeData(p_item);
                activateItemNode(data);
            });

    m_slaveNavigationWrapper.reset(new NavigationModeWrapper<QListWidget, QListWidgetItem>(m_slaveExplorer));
    NavigationModeMgr::getInst().registerNavigationTarget(m_slaveNavigationWrapper.data());
}

void NotebookNodeExplorer::setNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    if (p_notebook == m_notebook) {
        return;
    }

    if (m_notebook) {
        disconnect(m_notebook.data(), nullptr, this, nullptr);
    }

    cacheState(true);

    m_notebook = p_notebook;

    if (m_notebook) {
        connect(m_notebook.data(), &Notebook::nodeUpdated,
                this, [this](const Node *p_node) {
                    updateNode(p_node->getParent());
                });
    }

    generateMasterNodeTree();
}

void NotebookNodeExplorer::generateMasterNodeTree()
{
    m_masterExplorer->clear();

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
    bool restored = false;
    auto &cacheData = getCache();
    auto curMasterNode = cacheData.m_masterStateCache->getCurrentItem();
    if (curMasterNode) {
        restored = true;
        setCurrentMasterNode(curMasterNode);
    } else if (!isCombinedExploreMode()) {
        // Manually update slave explorer on first run of generation.
        updateSlaveExplorer();
    }
    if (!cacheData.m_currentSlaveName.isEmpty() && !isCombinedExploreMode()) {
        restored = true;
        setCurrentSlaveNode(cacheData.m_currentSlaveName);
    }
    if (!restored) {
        focusNormalNode();
    }

   cacheData.clear();
}

void NotebookNodeExplorer::loadRootNode(const Node *p_node) const
{
    Q_ASSERT(p_node->isLoaded() && p_node->isContainer());

    // External children.
    if (m_externalFilesVisible) {
        auto externalChildren = p_node->fetchExternalChildren();
        // TODO: Sort external children.
        for (const auto &child : externalChildren) {
            if (!belongsToMasterExplorer(child.data())) {
                continue;
            }

            auto item = new QTreeWidgetItem(m_masterExplorer);
            loadMasterExternalNode(item, child);
        }
    }

    // Children.
    auto children = p_node->getChildren();
    sortNodes(children);
    for (const auto &child : children) {
        if (!belongsToMasterExplorer(child.data())) {
            continue;
        }

        auto item = new QTreeWidgetItem(m_masterExplorer);
        loadMasterNode(item, child.data(), 1);
    }
}

static void clearTreeWidgetItemChildren(QTreeWidgetItem *p_item)
{
    auto children = p_item->takeChildren();
    for (auto &child : children) {
        delete child;
    }
}

void NotebookNodeExplorer::loadMasterNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (!p_node->isLoaded()) {
        p_node->load();
    }

    clearTreeWidgetItemChildren(p_item);

    fillMasterItem(p_item, p_node, p_level > 0);

    loadMasterNodeChildren(p_item, p_node, p_level - 1);

    if (getCache().m_masterStateCache->contains(p_item) && p_item->childCount() > 0) {
        if (p_item->isExpanded()) {
            loadMasterItemChildren(p_item);
        } else {
            // itemExpanded() will trigger loadMasterItemChildren().
            p_item->setExpanded(true);
        }
    }
}

void NotebookNodeExplorer::loadMasterExternalNode(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const
{
    clearTreeWidgetItemChildren(p_item);

    fillMasterItem(p_item, p_node);

    // No children for external node.
}

void NotebookNodeExplorer::loadMasterNodeChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (p_level < 0) {
        return;
    }

    // External children.
    if (m_externalFilesVisible && p_node->isContainer()) {
        auto externalChildren = p_node->fetchExternalChildren();
        // TODO: Sort external children.
        for (const auto &child : externalChildren) {
            if (!belongsToMasterExplorer(child.data())) {
                continue;
            }

            auto item = new QTreeWidgetItem(p_item);
            loadMasterExternalNode(item, child);
        }
    }

    auto children = p_node->getChildren();
    sortNodes(children);
    for (const auto &child : children) {
        if (!belongsToMasterExplorer(child.data())) {
            continue;
        }

        auto item = new QTreeWidgetItem(p_item);
        loadMasterNode(item, child.data(), p_level);
    }
}

void NotebookNodeExplorer::fillMasterItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const
{
    setItemNodeData(p_item, NodeData(p_node, p_loaded));
    p_item->setText(Column::Name, p_node->getName());
    p_item->setIcon(Column::Name, getIcon(p_node));
    p_item->setToolTip(Column::Name, generateToolTip(p_node));
}

void NotebookNodeExplorer::fillMasterItem(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const
{
    setItemNodeData(p_item, NodeData(p_node));
    p_item->setText(Column::Name, p_node->getName());
    p_item->setIcon(Column::Name, getIcon(p_node.data()));
    p_item->setToolTip(Column::Name, tr("[External] %1").arg(p_node->getName()));
}

void NotebookNodeExplorer::fillSlaveItem(QListWidgetItem *p_item, Node *p_node) const
{
    setItemNodeData(p_item, NodeData(p_node, true));
    p_item->setText(p_node->getName());
    p_item->setIcon(getIcon(p_node));
    p_item->setToolTip(generateToolTip(p_node));
}

void NotebookNodeExplorer::fillSlaveItem(QListWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const
{
    setItemNodeData(p_item, NodeData(p_node));
    p_item->setText(p_node->getName());
    p_item->setIcon(getIcon(p_node.data()));
    p_item->setToolTip(tr("[External] %1").arg(p_node->getName()));
}

const QIcon &NotebookNodeExplorer::getIcon(const Node *p_node) const
{
    if (p_node->hasContent()) {
        return p_node->exists() ? s_nodeIcons[NodeIcon::FileNode] : s_nodeIcons[NodeIcon::InvalidFileNode];
    } else {
        return p_node->exists() ? s_nodeIcons[NodeIcon::FolderNode] : s_nodeIcons[NodeIcon::InvalidFolderNode];
    }
}

const QIcon &NotebookNodeExplorer::getIcon(const ExternalNode *p_node) const
{
    return p_node->isFolder() ? s_nodeIcons[NodeIcon::ExternalFolderNode] : s_nodeIcons[NodeIcon::ExternalFileNode];
}

Node *NotebookNodeExplorer::getCurrentMasterNode() const
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

Node *NotebookNodeExplorer::getCurrentSlaveNode() const
{
    auto item = m_slaveExplorer->currentItem();
    if (item) {
        auto data = getItemNodeData(item);
        if (data.isNode()) {
            return data.getNode();
        }
    }

    return nullptr;
}

NotebookNodeExplorer::NodeData NotebookNodeExplorer::getCurrentMasterNodeData() const
{
    auto item = m_masterExplorer->currentItem();
    if (item) {
        return getItemNodeData(item);
    }
    return NodeData();
}

NotebookNodeExplorer::NodeData NotebookNodeExplorer::getCurrentSlaveNodeData() const
{
    auto item = m_slaveExplorer->currentItem();
    if (item) {
        return getItemNodeData(item);
    }
    return NodeData();
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

void NotebookNodeExplorer::setItemNodeData(QListWidgetItem *p_item, const NodeData &p_data)
{
    p_item->setData(Qt::UserRole, QVariant::fromValue(p_data));
}

NotebookNodeExplorer::NodeData NotebookNodeExplorer::getItemNodeData(const QListWidgetItem *p_item)
{
    if (!p_item) {
        return NodeData();
    }

    return p_item->data(Qt::UserRole).value<NotebookNodeExplorer::NodeData>();
}

void NotebookNodeExplorer::updateNode(Node *p_node)
{
    if (p_node && p_node->getNotebook() != m_notebook) {
        return;
    }

    if (p_node && !belongsToMasterExplorer(p_node)) {
        updateSlaveExplorer();
        return;
    }

    auto item = findMasterNode(p_node);
    if (item) {
        bool expanded = item->isExpanded();
        item->setExpanded(false);
        loadMasterNode(item, p_node, 1);
        item->setExpanded(expanded);

        if (!isCombinedExploreMode()) {
            updateSlaveExplorer();
        }
    } else {
        cacheState(false);

        generateMasterNodeTree();
    }
}

// TODO: we could do it faster by going from the root node directly.
QTreeWidgetItem *NotebookNodeExplorer::findMasterNode(const Node *p_node) const
{
    if (!p_node) {
        return nullptr;
    }

    auto cnt = m_masterExplorer->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        auto item = findMasterNode(m_masterExplorer->topLevelItem(i), p_node);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QTreeWidgetItem *NotebookNodeExplorer::findMasterNode(QTreeWidgetItem *p_item, const Node *p_node) const
{
    auto data = getItemNodeData(p_item);
    if (data.matched(p_node)) {
        return p_item;
    }

    auto cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        auto item = findMasterNode(p_item->child(i), p_node);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QListWidgetItem *NotebookNodeExplorer::findSlaveNode(const Node *p_node) const
{
    for (int i = 0; i < m_slaveExplorer->count(); ++i) {
        auto data = getItemNodeData(m_slaveExplorer->item(i));
        if (data.matched(p_node)) {
            return m_slaveExplorer->item(i);
        }
    }
    return nullptr;
}

void NotebookNodeExplorer::setCurrentNode(Node *p_node)
{
    if (!p_node) {
        m_masterExplorer->setCurrentItem(nullptr);
        return;
    }

    Q_ASSERT(p_node->getNotebook() == m_notebook);

    if (belongsToMasterExplorer(p_node)) {
        setCurrentMasterNode(p_node);
    } else {
        setCurrentMasterNode(p_node->getParent());

        setCurrentSlaveNode(p_node);
    }
}

void NotebookNodeExplorer::setCurrentMasterNode(Node *p_node)
{
    if (!p_node || p_node->isRoot()) {
        m_masterExplorer->setCurrentItem(nullptr);
        return;
    }

    Q_ASSERT(p_node && belongsToMasterExplorer(p_node));

    // (rootNode, p_node].
    QList<Node *> nodes;
    auto node = p_node;
    while (!node->isRoot()) {
        nodes.push_front(node);
        node = node->getParent();
    }

    QList<QTreeWidgetItem *> items;
    auto nodeIt = nodes.constBegin();
    auto item = findMasterNodeInTopLevelItems(m_masterExplorer, *nodeIt);
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
            loadMasterNode(item, data.getNode(), 1);
        }

        auto childItem = findMasterNodeInDirectChildren(item, *nodeIt);
        if (!childItem) {
            return;
        }
        item = childItem;
        items.push_back(item);
        ++nodeIt;
    }

    Q_ASSERT(getItemNodeData(item).getNode() == p_node);

    // Do not expand the last item.
    for (int i = 0; i < items.size() - 1; ++i) {
        items[i]->setExpanded(true);
    }

    m_masterExplorer->setCurrentItem(item);
}

QTreeWidgetItem *NotebookNodeExplorer::findMasterNodeInDirectChildren(QTreeWidgetItem *p_item, const Node *p_node) const
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

QTreeWidgetItem *NotebookNodeExplorer::findMasterNodeInTopLevelItems(QTreeWidget *p_tree, const Node *p_node) const
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

void NotebookNodeExplorer::setCurrentSlaveNode(const Node *p_node)
{
    if (!p_node) {
        m_slaveExplorer->setCurrentItem(nullptr);
        return;
    }

    Q_ASSERT(!belongsToMasterExplorer(p_node));

    ListWidget::forEachItem(m_slaveExplorer, [this, p_node](QListWidgetItem *item) {
        auto data = getItemNodeData(item);
        if (data.matched(p_node)) {
            m_slaveExplorer->setCurrentItem(item);
            return false;
        }
        return true;
    });

    if (m_slaveExplorer->currentItem() && m_masterExplorer->hasFocus()) {
        // To get focus after creating a new note.
        m_slaveExplorer->setFocus();
    }
}

void NotebookNodeExplorer::setCurrentSlaveNode(const QString &p_name)
{
    if (p_name.isEmpty()) {
        m_slaveExplorer->setCurrentItem(nullptr);
        return;
    }

    ListWidget::forEachItem(m_slaveExplorer, [this, &p_name](QListWidgetItem *item) {
        auto data = getItemNodeData(item);
        if (data.matched(p_name)) {
            m_slaveExplorer->setCurrentItem(item);
            return false;
        }
        return true;
    });
}

void NotebookNodeExplorer::cacheState(bool p_saveCurrent)
{
    if (m_notebook) {
        auto &cacheData = getCache();
        cacheData.m_masterStateCache->save(m_masterExplorer, p_saveCurrent);
        if (p_saveCurrent && !isCombinedExploreMode()) {
            cacheData.m_currentSlaveName.clear();
            auto item = m_slaveExplorer->currentItem();
            if (item) {
                auto data = getItemNodeData(item);
                if (data.isNode()) {
                    cacheData.m_currentSlaveName = data.getNode()->getName();
                } else {
                    cacheData.m_currentSlaveName = data.getExternalNode()->getName();
                }
            }
        }
    }
}

NotebookNodeExplorer::CacheData &NotebookNodeExplorer::getCache() const
{
    Q_ASSERT(m_notebook);
    auto it = const_cast<NotebookNodeExplorer *>(this)->m_cache.find(m_notebook.data());
    if (it == const_cast<NotebookNodeExplorer *>(this)->m_cache.end()) {
        auto keyFunc = [](const QTreeWidgetItem *p_item, bool &p_ok) {
                           auto data = NotebookNodeExplorer::getItemNodeData(p_item);
                           if (data.isNode()) {
                               p_ok = true;
                               return data.getNode();
                           }

                           p_ok = false;
                           return static_cast<Node *>(nullptr);
                       };
        it = const_cast<NotebookNodeExplorer *>(this)->m_cache.insert(m_notebook.data(), CacheData());
        it.value().m_masterStateCache = QSharedPointer<QTreeWidgetStateCache<Node *>>::create(keyFunc);
    }

    return it.value();
}

void NotebookNodeExplorer::clearCache(const Notebook *p_notebook)
{
    auto it = m_cache.find(p_notebook);
    if (it != m_cache.end()) {
        it.value().clear();
    }
}

void NotebookNodeExplorer::createMasterContextMenuOnRoot(QMenu *p_menu)
{
    createAndAddAction(Action::NewNote, p_menu);

    createAndAddAction(Action::NewFolder, p_menu);

    if (isPasteOnNodeAvailable(nullptr)) {
        p_menu->addSeparator();
        createAndAddAction(Action::Paste, p_menu);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::Reload, p_menu);

    createAndAddAction(Action::ReloadIndex, p_menu);

    createAndAddAction(Action::OpenLocation, p_menu);
}

void NotebookNodeExplorer::createContextMenuOnNode(QMenu *p_menu, const Node *p_node, bool p_master)
{
    const int selectedSize = p_master ? m_masterExplorer->selectedItems().size() : m_slaveExplorer->selectedItems().size();

    createAndAddAction(Action::Edit, p_menu, p_master);

    createAndAddAction(Action::Read, p_menu, p_master);

    addOpenWithMenu(p_menu, p_master);

    p_menu->addSeparator();

    if (selectedSize == 1 && p_node->isContainer()) {
        createAndAddAction(Action::ExpandAll, p_menu, p_master);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::NewNote, p_menu, p_master);

    createAndAddAction(Action::NewFolder, p_menu, p_master);

    p_menu->addSeparator();

    createAndAddAction(Action::Copy, p_menu, p_master);

    createAndAddAction(Action::Cut, p_menu, p_master);

    if (selectedSize == 1 && isPasteOnNodeAvailable(p_node)) {
        createAndAddAction(Action::Paste, p_menu, p_master);
    }

    createAndAddAction(Action::Delete, p_menu, p_master);

    createAndAddAction(Action::RemoveFromConfig, p_menu, p_master);

    p_menu->addSeparator();

    createAndAddAction(Action::Reload, p_menu, p_master);

    createAndAddAction(Action::Sort, p_menu, p_master);

    if (selectedSize == 1
        && m_notebook->tag()
        && !p_node->isContainer()) {
        p_menu->addSeparator();

        createAndAddAction(Action::Tag, p_menu, p_master);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::PinToQuickAccess, p_menu, p_master);

    if (selectedSize == 1) {
        createAndAddAction(Action::CopyPath, p_menu, p_master);

        createAndAddAction(Action::OpenLocation, p_menu, p_master);

        createAndAddAction(Action::Properties, p_menu, p_master);
    }
}

void NotebookNodeExplorer::createContextMenuOnExternalNode(QMenu *p_menu, const ExternalNode *p_node, bool p_master)
{
    Q_UNUSED(p_node);

    const int selectedSize = p_master ? m_masterExplorer->selectedItems().size() : m_slaveExplorer->selectedItems().size();

    createAndAddAction(Action::Edit, p_menu, p_master);

    createAndAddAction(Action::Read, p_menu, p_master);

    addOpenWithMenu(p_menu, p_master);

    p_menu->addSeparator();

    createAndAddAction(Action::ImportToConfig, p_menu, p_master);

    p_menu->addSeparator();

    createAndAddAction(Action::PinToQuickAccess, p_menu, p_master);

    if (selectedSize == 1) {
        createAndAddAction(Action::CopyPath, p_menu, p_master);

        createAndAddAction(Action::OpenLocation, p_menu, p_master);
    }
}

void NotebookNodeExplorer::createSlaveContextMenuOnMasterNode(QMenu *p_menu)
{
    auto masterNode = getSlaveExplorerMasterNode();
    if (!masterNode) {
        // Current master node may be an external node.
        return;
    }

    createAndAddAction(Action::NewNote, p_menu, false);

    createAndAddAction(Action::NewFolder, p_menu, false);

    if (isPasteOnNodeAvailable(masterNode)) {
        p_menu->addSeparator();
        createAndAddAction(Action::Paste, p_menu, false);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::Reload, p_menu, false);

    createAndAddAction(Action::OpenLocation, p_menu, false);
}

static QIcon generateMenuActionIcon(const QString &p_name)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    return IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(p_name));
}

QAction *NotebookNodeExplorer::createAction(Action p_act, QObject *p_parent, bool p_master)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    QAction *act = nullptr;
    switch (p_act) {
    case Action::NewNote:
        act = new QAction(generateMenuActionIcon("new_note.svg"),
                          tr("New &Note"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, []() {
                    emit VNoteX::getInst().newNoteRequested();
                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::NewNote));
        break;

    case Action::NewFolder:
        act = new QAction(generateMenuActionIcon("new_folder.svg"),
                          tr("New &Folder"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, []() {
                    emit VNoteX::getInst().newFolderRequested();
                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::NewFolder));
        break;

    case Action::Properties:
        act = new QAction(generateMenuActionIcon("properties.svg"),
                          tr("&Properties (Rename)"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    openCurrentNodeProperties(p_master);
                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Properties));
        break;

    case Action::OpenLocation:
        act = new QAction(tr("Open Locat&ion"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    // Always use the master node no matter it is in master/slave explorer.
                    auto item = m_masterExplorer->currentItem();
                    if (!item) {
                        if (m_notebook) {
                            auto locationPath = m_notebook->getRootFolderAbsolutePath();
                            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(locationPath));
                        }
                        return;
                    }

                    auto data = getItemNodeData(item);
                    Node *node = nullptr;
                    if (data.isNode()) {
                        node = data.getNode();
                    } else {
                        // Open the parent folder of the external node.
                        node = data.getExternalNode()->getNode();
                    }

                    if (checkInvalidNode(node)) {
                        return;
                    }

                    auto locationPath = node->fetchAbsolutePath();
                    if (!node->isContainer()) {
                        locationPath = PathUtils::parentDirPath(locationPath);
                    }

                    if (!locationPath.isEmpty()) {
                        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(locationPath));
                    }
                });
        break;

    case Action::CopyPath:
        act = new QAction(tr("Cop&y Path"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    NodeData data = p_master ? getCurrentMasterNodeData() : getCurrentSlaveNodeData();
                    if (!data.isValid()) {
                        return;
                    }

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
                this, [this, p_master]() {
                    copySelectedNodes(false, p_master);
                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Copy));
        break;

    case Action::Cut:
        act = new QAction(tr("C&ut"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    copySelectedNodes(true, p_master);
                });
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Cut));
        break;

    case Action::Paste:
        act = new QAction(tr("&Paste"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::pasteNodesFromClipboard);
        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Paste));
        break;

    case Action::Delete:
        act = new QAction(tr("&Delete"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    removeSelectedNodes(p_master);
                });
        break;

    case Action::RemoveFromConfig:
        act = new QAction(tr("Remo&ve From Index"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    removeSelectedNodesFromConfig(p_master);
                });
        break;

    case Action::Sort:
        act = new QAction(generateMenuActionIcon("sort.svg"), tr("&Sort"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    manualSort(p_master);
                });
        break;

    case Action::Reload:
        act = new QAction(tr("Re&load"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto node = currentExploredFolderNode();
                    updateNode(node);
                });
        break;

    case Action::ReloadIndex:
        act = new QAction(tr("Relo&ad Index Of Notebook From Disk"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (!m_notebook) {
                        return;
                    }

                    auto event = QSharedPointer<Event>::create();
                    emit nodeAboutToReload(m_notebook->getRootNode().data(), event);
                    if (!event->m_response.toBool()) {
                        return;
                    }

                    m_notebook->reloadNodes();
                    reload();
                });
        break;

    case Action::ImportToConfig:
        act = new QAction(tr("&Import To Index"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    auto nodes = p_master ? getMasterSelectedNodesAndExternalNodes().second : getSlaveSelectedNodesAndExternalNodes().second;
                    importToIndex(nodes);
                });
        break;

    case Action::Open:
        // Use Edit and Read instead.
        Q_ASSERT(false);
        act = new QAction(tr("&Open"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    // Support nodes and external nodes.
                    // Do nothing for folders.
                    auto selectedNodes = p_master ? getMasterSelectedNodesAndExternalNodes() : getSlaveSelectedNodesAndExternalNodes();
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
                });
        break;

    case Action::Edit:
        Q_FALLTHROUGH();
    case Action::Read:
    {
        const bool isEdit = p_act == Action::Edit;
        act = new QAction(isEdit ? tr("&Edit") : tr("&Read"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master, isEdit]() {
                    // Support nodes and external nodes.
                    // Do nothing for folders.
                    auto selectedNodes = p_master ? getMasterSelectedNodesAndExternalNodes() : getSlaveSelectedNodesAndExternalNodes();
                    for (const auto &externalNode : selectedNodes.second) {
                        if (!externalNode->isFolder()) {
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            paras->m_mode = isEdit ? ViewWindowMode::Edit : ViewWindowMode::Read;
                            paras->m_forceMode = true;
                            emit fileActivated(externalNode->fetchAbsolutePath(), paras);
                        }
                    }

                    for (const auto &node : selectedNodes.first) {
                        if (checkInvalidNode(node)) {
                            continue;
                        }

                        if (node->hasContent()) {
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            paras->m_mode = isEdit ? ViewWindowMode::Edit : ViewWindowMode::Read;
                            paras->m_forceMode = true;
                            emit nodeActivated(node, paras);
                        }
                    }
                });
        break;
    }

    case Action::ExpandAll:
        act = new QAction(tr("E&xpand All\t*"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto item = m_masterExplorer->currentItem();
                    if (!item || item->childCount() == 0) {
                        return;
                    }
                    auto data = getItemNodeData(item);
                    if (!data.isNode()) {
                        return;
                    }

                    TreeWidget::expandRecursively(item);
                });
        break;

    case Action::PinToQuickAccess:
        act = new QAction(generateMenuActionIcon(QStringLiteral("quick_access_menu.svg")),
                          tr("Pin To &Quick Access"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    auto nodes = p_master ? getMasterSelectedNodesAndExternalNodes() : getSlaveSelectedNodesAndExternalNodes();
                    QStringList files;
                    for (const auto &node : nodes.first) {
                        files.push_back(node->fetchAbsolutePath());
                    }
                    for (const auto &node : nodes.second) {
                        files.push_back(node->fetchAbsolutePath());
                    }
                    if (!files.isEmpty()) {
                        emit VNoteX::getInst().pinToQuickAccessRequested(files);
                    }
                });
        break;

    case Action::Tag:
        act = new QAction(generateMenuActionIcon(QStringLiteral("tag.svg")), tr("&Tags"), p_parent);
        connect(act, &QAction::triggered,
                this, [this, p_master]() {
                    Q_ASSERT(m_notebook->tag());
                    auto node = p_master ? getCurrentMasterNode() : getCurrentSlaveNode();
                    Q_ASSERT(node);
                    if (checkInvalidNode(node)) {
                        return;
                    }
                    ViewTagsDialog dialog(node, VNoteX::getInst().getMainWindow());
                    dialog.exec();
                });
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    return act;
}

QAction *NotebookNodeExplorer::createAndAddAction(Action p_act, QMenu *p_menu, bool p_master)
{
    auto act = createAction(p_act, p_menu, p_master);
    p_menu->addAction(act);
    return act;
}

void NotebookNodeExplorer::copySelectedNodes(bool p_move, bool p_master)
{
    auto nodes = p_master ? getMasterSelectedNodesAndExternalNodes().first : getSlaveSelectedNodesAndExternalNodes().first;
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

QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> NotebookNodeExplorer::getMasterSelectedNodesAndExternalNodes() const
{
    QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> nodes;

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

QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> NotebookNodeExplorer::getSlaveSelectedNodesAndExternalNodes() const
{
    QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> nodes;

    auto items = m_slaveExplorer->selectedItems();
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
    auto destNode = getCurrentMasterNode();
    if (!destNode) {
        if (!m_notebook) {
            return;
        }
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
        clearCache(node->getNotebook());
    }

    // Update and expand dest node. Select all pasted nodes.
    updateAndExpandNode(destNode);
    selectNodes(pastedNodes);

    if (isMove) {
        ClipboardUtils::clearClipboard();
    }

    VNoteX::getInst().showStatusMessageShort(tr("Pasted %n item(s)", "", pastedNodes.size()));
}

void NotebookNodeExplorer::setMasterNodeExpanded(const Node *p_node, bool p_expanded)
{
    auto item = findMasterNode(p_node);
    if (item) {
        item->setExpanded(p_expanded);
    }
}

void NotebookNodeExplorer::selectNodes(const QVector<const Node *> &p_nodes)
{
    if (p_nodes.isEmpty()) {
        return;
    }

    // All the nodes should either belong to master or slave explorer.
    if (belongsToMasterExplorer(p_nodes[0])) {
        bool firstItem = true;
        for (auto node : p_nodes) {
            auto item = findMasterNode(node);
            if (item) {
                auto flags = firstItem ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select;
                m_masterExplorer->setCurrentItem(item, 0, flags);
                firstItem = false;
            }
        }
    } else {
        bool firstItem = true;
        for (auto node : p_nodes) {
            auto item = findSlaveNode(node);
            if (item) {
                auto flags = firstItem ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select;
                m_slaveExplorer->setCurrentItem(item, flags);
                firstItem = false;
            }
        }
    }
}

void NotebookNodeExplorer::removeSelectedNodes(bool p_master)
{
    const QString text = tr("Delete these folders and notes?");
    const QString info = tr("Deleted files could be found in the recycle bin of notebook.");
    auto nodes = confirmSelectedNodes(tr("Confirm Deletion"), text, info, p_master);
    removeNodes(nodes, false);
}

QVector<Node *> NotebookNodeExplorer::confirmSelectedNodes(const QString &p_title,
                                                           const QString &p_text,
                                                           const QString &p_info,
                                                           bool p_master) const
{
    auto nodes = p_master ? getMasterSelectedNodesAndExternalNodes().first : getSlaveSelectedNodesAndExternalNodes().first;
    if (nodes.isEmpty()) {
        return nodes;
    }

    QVector<ConfirmItemInfo> items;
    for (const auto &node : nodes) {
        items.push_back(ConfirmItemInfo(getIcon(node),
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

void NotebookNodeExplorer::removeNodes(QVector<Node *> p_nodes, bool p_configOnly)
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

            if (p_configOnly) {
                m_notebook->removeNode(node, false, true);
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

    VNoteX::getInst().showStatusMessageShort(tr("Deleted/Removed %n item(s)", "", nrDeleted));
}

void NotebookNodeExplorer::removeSelectedNodesFromConfig(bool p_master)
{
    auto nodes = confirmSelectedNodes(tr("Confirm Removal"),
                                      tr("Remove these folders and notes from index?"),
                                      tr("Files are not touched but just removed from notebook index."),
                                      p_master);
    removeNodes(nodes, true);
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
    setMasterNodeExpanded(p_node, false);
    updateNode(p_node);
    setMasterNodeExpanded(p_node, true);
}

bool NotebookNodeExplorer::isMasterAllSelectedItemsSameType() const
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

    return true;
}

bool NotebookNodeExplorer::isSlaveAllSelectedItemsSameType() const
{
    auto items = m_slaveExplorer->selectedItems();
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

    return true;
}

void NotebookNodeExplorer::reload()
{
    updateNode(nullptr);
}

void NotebookNodeExplorer::focusNormalNode()
{
    if (isCombinedExploreMode()) {
        m_masterExplorer->setCurrentItem(m_masterExplorer->topLevelItem(0));
    } else {
        if (m_slaveExplorer->count() > 0) {
            m_slaveExplorer->setCurrentRow(0);
        }
    }
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

void NotebookNodeExplorer::sortNodes(QVector<QSharedPointer<Node>> &p_nodes, int p_start, int p_end, ViewOrder p_viewOrder) const
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
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> &p_b) {
            if (reversed) {
                return p_b->getName().toLower() < p_a->getName().toLower();
            } else {
                return p_a->getName().toLower() < p_b->getName().toLower();
            }
        });
        break;

    case ViewOrder::OrderedByCreatedTimeReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByCreatedTime:
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> &p_b) {
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
        std::sort(p_nodes.begin() + p_start, p_nodes.begin() + p_end, [reversed](const QSharedPointer<Node> &p_a, const QSharedPointer<Node> &p_b) {
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

void NotebookNodeExplorer::manualSort(bool p_master)
{
    auto node = p_master ? getCurrentMasterNode() : getCurrentSlaveNode();
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
            const bool selected = sortFolders ? child->isContainer() : !child->isContainer();
            if (selected) {
                selectedIdx.push_back(i);

                QStringList cols {child->getName(),
                                  Utils::dateTimeString(child->getCreatedTimeUtc().toLocalTime()),
                                  Utils::dateTimeString(child->getModifiedTimeUtc().toLocalTime())};
                QStringList comparisonCols {QString(),
                                            Utils::dateTimeStringUniform(child->getCreatedTimeUtc().toLocalTime()),
                                            Utils::dateTimeStringUniform(child->getModifiedTimeUtc().toLocalTime())};
                auto item = sortDlg.addItem(cols, comparisonCols);
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

    auto node = getCurrentMasterNode();
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

Node *NotebookNodeExplorer::currentExploredNode() const
{
    if (!m_notebook) {
        return nullptr;
    }

    if (isCombinedExploreMode()) {
        return getCurrentMasterNode();
    } else {
        auto node = getCurrentSlaveNode();
        if (!node) {
            node = getCurrentMasterNode();
        }
        return node;
    }
}

void NotebookNodeExplorer::setViewOrder(int p_order)
{
    if (m_viewOrder == p_order) {
        return;
    }

    if (p_order >= 0 && p_order < ViewOrder::ViewOrderMax) {
        m_viewOrder = static_cast<ViewOrder>(p_order);
        reload();
    }
}

void NotebookNodeExplorer::setExploreMode(int p_mode)
{
    if (m_exploreMode == p_mode) {
        return;
    }

    if (p_mode >= 0 && p_mode < ExploreMode::ExploreModeMax) {
        m_exploreMode = static_cast<ExploreMode>(p_mode);
        switch (m_exploreMode) {
        case ExploreMode::Combined:
            setFocusProxy(m_masterExplorer);

            WidgetUtils::distributeWidgetsOfSplitter(m_splitter);

            Q_ASSERT(m_slaveExplorer);
            m_slaveExplorer->clear();
            m_slaveExplorer->hide();

            disconnect(m_masterExplorer, &QTreeWidget::currentItemChanged,
                       this, &NotebookNodeExplorer::updateSlaveExplorer);
            break;

        case ExploreMode::SeparateSingle:
            Q_FALLTHROUGH();
        case ExploreMode::SeparateDouble:
            if (!m_slaveExplorer) {
                setupSlaveExplorer();
            }

            setFocusProxy(m_slaveExplorer);

            m_slaveExplorer->show();
            m_splitter->setOrientation(m_exploreMode == ExploreMode::SeparateSingle ? Qt::Vertical : Qt::Horizontal);
            WidgetUtils::distributeWidgetsOfSplitter(m_splitter);

            connect(m_masterExplorer, &QTreeWidget::currentItemChanged,
                    this, &NotebookNodeExplorer::updateSlaveExplorer);
            break;

        default:
            Q_ASSERT(false);
            return;
        }

        reload();
    }
}

QStringList NotebookNodeExplorer::getSelectedNodesPath(bool p_master) const
{
    QStringList files;

    // Support nodes and external nodes.
    auto selectedNodes = p_master ? getMasterSelectedNodesAndExternalNodes() : getSlaveSelectedNodesAndExternalNodes();
    for (const auto &externalNode : selectedNodes.second) {
        files << externalNode->fetchAbsolutePath();
    }

    for (const auto &node : selectedNodes.first) {
        if (checkInvalidNode(node)) {
            continue;
        }
        files << node->fetchAbsolutePath();
    }

    return files;
}

QSharedPointer<Node> NotebookNodeExplorer::importToIndex(QSharedPointer<ExternalNode> p_node)
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

void NotebookNodeExplorer::importToIndex(const QVector<QSharedPointer<ExternalNode>> &p_nodes)
{
    QSet<Node *> nodesToUpdate;
    Node *currentNode = nullptr;

    for (const auto &externalNode : p_nodes) {
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

bool NotebookNodeExplorer::checkInvalidNode(Node *p_node) const
{
    if (!p_node) {
        return true;
    }

    bool nodeExists = p_node->exists();
    if (nodeExists) {
        p_node->checkExists();
        nodeExists = p_node->exists();
    }

    if (!nodeExists) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Invalid node (%1).").arg(p_node->getName()),
                                 tr("Please check if the node exists on the disk."),
                                 p_node->fetchAbsolutePath(),
                                 VNoteX::getInst().getMainWindow());
        return true;
    }

    return false;
}

void NotebookNodeExplorer::addOpenWithMenu(QMenu *p_menu, bool p_master)
{
    auto subMenu = p_menu->addMenu(tr("Open &With"));

    const auto &types = FileTypeHelper::getInst().getAllFileTypes();

    for (const auto &ft : types) {
        if (ft.m_type == FileType::Others) {
            continue;
        }

        QAction *act = subMenu->addAction(ft.m_displayName);
        connect(act, &QAction::triggered,
                this, [this, act, p_master]() {
                    openSelectedNodesWithProgram(act->data().toString(), p_master);
                });
        act->setData(ft.m_typeName);
    }

    subMenu->addSeparator();

    {
        const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
        for (const auto &pro : sessionConfig.getExternalPrograms()) {
            QAction *act = subMenu->addAction(pro.m_name);
            connect(act, &QAction::triggered,
                    this, [this, act, p_master]() {
                        openSelectedNodesWithProgram(act->data().toString(), p_master);
                    });
            act->setData(pro.m_name);
            WidgetUtils::addActionShortcutText(act, pro.m_shortcut);
        }
    }

    subMenu->addSeparator();

    {
        auto defaultAct = subMenu->addAction(tr("System Default Program"));
        connect(defaultAct, &QAction::triggered,
                this, [this, p_master]() {
                    openSelectedNodesWithProgram(QString(), p_master);
                });
        const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
        WidgetUtils::addActionShortcutText(defaultAct, coreConfig.getShortcut(CoreConfig::OpenWithDefaultProgram));
    }

    subMenu->addAction(tr("Add External Program"),
                       this,
                       []() {
                            const auto file = DocsUtils::getDocFile(QStringLiteral("external_programs.md"));
                            if (!file.isEmpty()) {
                                auto paras = QSharedPointer<FileOpenParameters>::create();
                                paras->m_readOnly = true;
                                emit VNoteX::getInst().openFileRequested(file, paras);
                            }
                       });
}

// Shortcut auxiliary, it can also be used to determine the browser.
bool NotebookNodeExplorer::isActionFromMaster() const
{
    if (!isCombinedExploreMode()) {
        return m_masterExplorer->hasFocus();
    }
    return true;
}

void NotebookNodeExplorer::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // OpenWithDefaultProgram.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OpenWithDefaultProgram),
                                                    this,
                                                    Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        openSelectedNodesWithProgram(QString(), isActionFromMaster());
                    });
        }
    }

    // Copy
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Copy),
                                                    this,
                                                    Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        copySelectedNodes(false, isActionFromMaster());
                    });
        }
    }

    // Cut
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Cut),
                                                    this,
                                                    Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        copySelectedNodes(true, isActionFromMaster());
                    });
        }
    }

    // Paste
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Paste),
                                                    this,
                                                    Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &NotebookNodeExplorer::pasteNodesFromClipboard);
        }
    }

    // Properties
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Properties),
                                                    this,
                                                    Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut,  &QShortcut::activated,
                    this, [this]() {
                        openCurrentNodeProperties(isActionFromMaster());
                    });
        }
    }

    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    for (const auto &pro : sessionConfig.getExternalPrograms()) {
        auto shortcut = WidgetUtils::createShortcut(pro.m_shortcut, this, Qt::WidgetWithChildrenShortcut);
        const auto &name = pro.m_name;
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this, name]() {
                        bool isMaster = true;
                        if (!isCombinedExploreMode()) {
                            isMaster = m_masterExplorer->hasFocus();
                        }
                        openSelectedNodesWithProgram(name, isMaster);
                    });
        }
    }
}

void NotebookNodeExplorer::openSelectedNodesWithProgram(const QString &p_name, bool p_master)
{
    const bool closeBefore = ConfigMgr::getInst().getWidgetConfig().getNodeExplorerCloseBeforeOpenWithEnabled();
    const auto files = getSelectedNodesPath(p_master);
    for (const auto &file : files) {
        if (file.isEmpty()) {
            continue;
        }

        if (closeBefore) {
            auto event = QSharedPointer<Event>::create();
            emit closeFileRequested(file, event);
            if (!event->m_response.toBool()) {
                continue;
            }
        }

        if (p_name.isEmpty()) {
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(file));
        } else {
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_fileType = p_name;
            emit VNoteX::getInst().openFileRequested(file, paras);
        }
    }
}

void NotebookNodeExplorer::openCurrentNodeProperties(bool p_master)
{
    const int selectedSize = p_master ? m_masterExplorer->selectedItems().size() : m_slaveExplorer->selectedItems().size();
    if (selectedSize != 1) {
        return;
    }
    auto node = p_master ? getCurrentMasterNode() : getCurrentSlaveNode();
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
}

void NotebookNodeExplorer::loadMasterItemChildren(QTreeWidgetItem *p_item) const
{
    auto cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        auto child = p_item->child(i);
        auto data = getItemNodeData(child);
        if (data.isNode() && !data.isLoaded()) {
            loadMasterNode(child, data.getNode(), 1);
        }
    }
}

QString NotebookNodeExplorer::generateToolTip(const Node *p_node)
{
    Q_ASSERT(p_node->isLoaded());
    const QString tip = p_node->exists() ? p_node->getName() : (tr("[Invalid] %1").arg(p_node->getName()));
    return tip;
}

QByteArray NotebookNodeExplorer::saveState() const
{
    return m_splitter->saveState();
}

void NotebookNodeExplorer::restoreState(const QByteArray &p_data)
{
    m_splitter->restoreState(p_data);
}

bool NotebookNodeExplorer::belongsToMasterExplorer(const Node *p_node) const
{
    switch (m_exploreMode) {
    case ExploreMode::Combined:
        return true;

    case ExploreMode::SeparateSingle:
        Q_FALLTHROUGH();
    case ExploreMode::SeparateDouble:
        return p_node ? p_node->isContainer() : true;
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    return true;
}

bool NotebookNodeExplorer::belongsToMasterExplorer(const ExternalNode *p_node) const
{
    if (isCombinedExploreMode()) {
        return true;
    } else {
        return p_node ? p_node->isFolder() : false;
    }
}

void NotebookNodeExplorer::updateSlaveExplorer()
{
    Q_ASSERT(!isCombinedExploreMode());
    m_slaveExplorer->clear();

    const Node *masterNode = nullptr;
    auto item = m_masterExplorer->currentItem();
    if (item) {
        const int selectedSize = m_masterExplorer->selectedItems().size();
        if (selectedSize > 1) {
            return;
        }

        auto data = getItemNodeData(item);
        if (data.isNode()) {
            masterNode = data.getNode();
            Q_ASSERT(masterNode->isContainer());
        }
    } else {
        // Root node.
        masterNode = m_notebook ? m_notebook->getRootNode().data() : nullptr;
    }

    if (!masterNode) {
        return;
    }

    Q_ASSERT(masterNode->isContainer() && masterNode->isLoaded());

    // External children.
    if (m_externalFilesVisible) {
        auto externalChildren = masterNode->fetchExternalChildren();
        // TODO: Sort external children.
        for (const auto &child : externalChildren) {
            if (child->isFolder()) {
                continue;
            }

            auto item = new QListWidgetItem(m_slaveExplorer);
            fillSlaveItem(item, child);
        }
    }

    auto children = masterNode->getChildren();
    sortNodes(children);
    for (const auto &child : children) {
        if (child->isContainer()) {
            continue;
        }

        Q_ASSERT(child->isLoaded());
        auto item = new QListWidgetItem(m_slaveExplorer);
        fillSlaveItem(item, child.data());
    }
}

bool NotebookNodeExplorer::isCombinedExploreMode() const
{
    return m_exploreMode == ExploreMode::Combined;
}

Node *NotebookNodeExplorer::getSlaveExplorerMasterNode() const
{
    Q_ASSERT(!isCombinedExploreMode());
    auto item = m_masterExplorer->currentItem();
    if (item) {
        const int selectedSize = m_masterExplorer->selectedItems().size();
        if (selectedSize > 1) {
            return nullptr;
        }
        return getCurrentMasterNode();
    } else {
        // Root node.
        return (m_notebook ? m_notebook->getRootNode().data() : nullptr);
    }
}
