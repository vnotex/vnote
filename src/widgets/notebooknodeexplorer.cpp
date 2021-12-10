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
#include <utils/processutils.h>
#include "treewidget.h"
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

bool NotebookNodeExplorer::NodeData::isLoaded() const
{
    return m_loaded;
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

    m_navigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_masterExplorer));
    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationWrapper.data());

    connect(m_masterExplorer, &QTreeWidget::itemExpanded,
            this, &NotebookNodeExplorer::loadItemChildren);

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
                        createContextMenuOnExternalNode(menu.data(), data.getExternalNode().data());
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
        focusNormalNode();
    }

    stateCache()->clear();
}

void NotebookNodeExplorer::loadRootNode(const Node *p_node) const
{
    Q_ASSERT(p_node->isLoaded() && p_node->isContainer());

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

    if (stateCache()->contains(p_item) && p_item->childCount() > 0) {
        if (p_item->isExpanded()) {
            loadItemChildren(p_item);
        } else {
            // itemExpanded() will trigger loadItemChildren().
            p_item->setExpanded(true);
        }
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

void NotebookNodeExplorer::fillTreeItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const
{
    setItemNodeData(p_item, NodeData(p_node, p_loaded));
    p_item->setText(Column::Name, p_node->getName());
    p_item->setIcon(Column::Name, getNodeItemIcon(p_node));
    p_item->setToolTip(Column::Name, generateToolTip(p_node));
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
        return p_node->exists() ? s_nodeIcons[NodeIcon::FileNode] : s_nodeIcons[NodeIcon::InvalidFileNode];
    } else {
        return p_node->exists() ? s_nodeIcons[NodeIcon::FolderNode] : s_nodeIcons[NodeIcon::InvalidFolderNode];
    }
}

const QIcon &NotebookNodeExplorer::getNodeItemIcon(const ExternalNode *p_node) const
{
    return p_node->isFolder() ? s_nodeIcons[NodeIcon::ExternalFolderNode] : s_nodeIcons[NodeIcon::ExternalFileNode];
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
        loadNode(item, p_node, 1);
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

void NotebookNodeExplorer::createContextMenuOnNode(QMenu *p_menu, const Node *p_node)
{
    const int selectedSize = m_masterExplorer->selectedItems().size();

    createAndAddAction(Action::Open, p_menu);

    addOpenWithMenu(p_menu);

    p_menu->addSeparator();

    if (selectedSize == 1 && p_node->isContainer()) {
        createAndAddAction(Action::ExpandAll, p_menu);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::NewNote, p_menu);

    createAndAddAction(Action::NewFolder, p_menu);

    p_menu->addSeparator();

    createAndAddAction(Action::Copy, p_menu);

    createAndAddAction(Action::Cut, p_menu);

    if (selectedSize == 1 && isPasteOnNodeAvailable(p_node)) {
        createAndAddAction(Action::Paste, p_menu);
    }

    createAndAddAction(Action::Delete, p_menu);

    createAndAddAction(Action::RemoveFromConfig, p_menu);

    p_menu->addSeparator();

    createAndAddAction(Action::Reload, p_menu);

    createAndAddAction(Action::ReloadIndex, p_menu);

    createAndAddAction(Action::Sort, p_menu);

    if (selectedSize == 1
        && m_notebook->tag()
        && !p_node->isContainer()) {
        p_menu->addSeparator();

        createAndAddAction(Action::Tag, p_menu);
    }

    p_menu->addSeparator();

    createAndAddAction(Action::PinToQuickAccess, p_menu);

    if (selectedSize == 1) {
        createAndAddAction(Action::CopyPath, p_menu);

        createAndAddAction(Action::OpenLocation, p_menu);

        createAndAddAction(Action::Properties, p_menu);
    }
}

void NotebookNodeExplorer::createContextMenuOnExternalNode(QMenu *p_menu, const ExternalNode *p_node)
{
    Q_UNUSED(p_node);

    const int selectedSize = m_masterExplorer->selectedItems().size();
    createAndAddAction(Action::Open, p_menu);

    addOpenWithMenu(p_menu);

    p_menu->addSeparator();

    createAndAddAction(Action::ImportToConfig, p_menu);

    p_menu->addSeparator();

    createAndAddAction(Action::PinToQuickAccess, p_menu);

    if (selectedSize == 1) {
        createAndAddAction(Action::CopyPath, p_menu);

        createAndAddAction(Action::OpenLocation, p_menu);
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
                          tr("New &Note"),
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
                          tr("&Properties (Rename)"),
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
        act = new QAction(tr("Open Locat&ion"), p_parent);
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
                        const auto &externalNode = data.getExternalNode();
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

    case Action::Delete:
        act = new QAction(tr("&Delete"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::removeSelectedNodes);
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
                    updateNode(node);
                });
        break;

    case Action::ReloadIndex:
        act = new QAction(tr("Relo&ad Index From Disk"), p_parent);
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

                    reload();
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

    case Action::ExpandAll:
        act = new QAction(tr("&Expand All\t*"), p_parent);
        connect(act, &QAction::triggered,
                this, &NotebookNodeExplorer::expandCurrentNodeAll);
        break;

    case Action::PinToQuickAccess:
        act = new QAction(generateMenuActionIcon(QStringLiteral("quick_access_menu.svg")),
                          tr("Pin To &Quick Access"),
                          p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto nodes = getSelectedNodes();
                    QStringList files;
                    bool hasFilteredAway = false;
                    for (const auto &node : nodes.first) {
                        if (node->hasContent()) {
                            files.push_back(node->fetchAbsolutePath());
                        } else {
                            hasFilteredAway = true;
                        }
                    }
                    for (const auto &node : nodes.second) {
                        if (!node->isFolder()) {
                            files.push_back(node->fetchAbsolutePath());
                        } else {
                            hasFilteredAway = true;
                        }
                    }
                    if (!files.isEmpty()) {
                        emit VNoteX::getInst().pinToQuickAccessRequested(files);
                    }
                    if (hasFilteredAway) {
                        VNoteX::getInst().showStatusMessageShort(tr("Folder is not supported by quick access"));
                    }
                });
        break;

    case Action::Tag:
        act = new QAction(generateMenuActionIcon(QStringLiteral("tag.svg")), tr("&Tags"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    auto item = m_masterExplorer->currentItem();
                    if (!item || !m_notebook->tag()) {
                        return;
                    }
                    auto data = getItemNodeData(item);
                    if (data.isNode()) {
                        auto node = data.getNode();
                        if (checkInvalidNode(node)) {
                            return;
                        }

                        ViewTagsDialog dialog(node, VNoteX::getInst().getMainWindow());
                        dialog.exec();
                    }
                });
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    return act;
}

QAction *NotebookNodeExplorer::createAndAddAction(Action p_act, QMenu *p_menu)
{
    auto act = createAction(p_act, p_menu);
    p_menu->addAction(act);
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

QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> NotebookNodeExplorer::getSelectedNodes() const
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

void NotebookNodeExplorer::removeSelectedNodes()
{
    const QString text = tr("Delete these folders and notes?");
    const QString info = tr("Deleted files could be found in the recycle bin of notebook.");
    auto nodes = confirmSelectedNodes(tr("Confirm Deletion"), text, info);
    removeNodes(nodes, false);
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

void NotebookNodeExplorer::removeSelectedNodesFromConfig()
{
    auto nodes = confirmSelectedNodes(tr("Confirm Removal"),
                                      tr("Remove these folders and notes from index?"),
                                      tr("Files are not touched but just removed from notebook index."));
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

    return true;
}

void NotebookNodeExplorer::reload()
{
    updateNode(nullptr);
}

void NotebookNodeExplorer::focusNormalNode()
{
    auto item = m_masterExplorer->currentItem();
    if (item) {
        return;
    }

    m_masterExplorer->setCurrentItem(m_masterExplorer->topLevelItem(0));
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
            const bool selected = sortFolders ? child->isContainer() : !child->isContainer();
            if (selected) {
                selectedIdx.push_back(i);

                QStringList cols {child->getName(),
                                  Utils::dateTimeString(child->getCreatedTimeUtc().toLocalTime()),
                                  Utils::dateTimeString(child->getModifiedTimeUtc().toLocalTime())};
                auto item = sortDlg.addItem(cols);
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

Node *NotebookNodeExplorer::currentExploredNode() const
{
    if (!m_notebook) {
        return nullptr;
    }

    return getCurrentNode();
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

QStringList NotebookNodeExplorer::getSelectedNodesPath() const
{
    QStringList files;

    // Support nodes and external nodes.
    auto selectedNodes = getSelectedNodes();
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

void NotebookNodeExplorer::expandCurrentNodeAll()
{
    auto item = m_masterExplorer->currentItem();
    if (!item || item->childCount() == 0) {
        return;
    }
    auto data = getItemNodeData(item);
    if (!data.isNode()) {
        return;
    }

    expandItemRecursively(item);
}

void NotebookNodeExplorer::expandItemRecursively(QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    p_item->setExpanded(true);
    const int cnt = p_item->childCount();
    if (cnt == 0) {
        return;
    }

    for (int i = 0; i < cnt; ++i) {
        expandItemRecursively(p_item->child(i));
    }
}

void NotebookNodeExplorer::addOpenWithMenu(QMenu *p_menu)
{
    auto subMenu = p_menu->addMenu(tr("Open &With"));

    {
        const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
        for (const auto &pro : sessionConfig.getExternalPrograms()) {
            QAction *act = subMenu->addAction(pro.m_name);
            connect(act, &QAction::triggered,
                    this, [this, act]() {
                        openSelectedNodesWithExternalProgram(act->data().toString());
                    });
            act->setData(pro.m_command);
            WidgetUtils::addActionShortcutText(act, pro.m_shortcut);
        }
    }

    subMenu->addSeparator();

    {
        auto defaultAct = subMenu->addAction(tr("System Default Program"),
                                             this,
                                             &NotebookNodeExplorer::openSelectedNodesWithDefaultProgram);
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

void NotebookNodeExplorer::setupShortcuts()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // OpenWithDefaultProgram.
    {
        auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::OpenWithDefaultProgram), this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &NotebookNodeExplorer::openSelectedNodesWithDefaultProgram);
        }
    }

    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    for (const auto &pro : sessionConfig.getExternalPrograms()) {
        auto shortcut = WidgetUtils::createShortcut(pro.m_shortcut, this);
        const auto &command = pro.m_command;
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this, command]() {
                        openSelectedNodesWithExternalProgram(command);
                    });
        }
    }
}

void NotebookNodeExplorer::openSelectedNodesWithDefaultProgram()
{
    const bool closeBefore = ConfigMgr::getInst().getWidgetConfig().getNodeExplorerCloseBeforeOpenWithEnabled();
   const auto files = getSelectedNodesPath();
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

       WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(file));
   }
}

void NotebookNodeExplorer::openSelectedNodesWithExternalProgram(const QString &p_command)
{
    const bool closeBefore = ConfigMgr::getInst().getWidgetConfig().getNodeExplorerCloseBeforeOpenWithEnabled();
    const auto files = getSelectedNodesPath();
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

        auto command = p_command;
        command.replace(QStringLiteral("%1"), QString("\"%1\"").arg(file));
        ProcessUtils::startDetached(command);
    }
}

void NotebookNodeExplorer::loadItemChildren(QTreeWidgetItem *p_item) const
{
    auto cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        auto child = p_item->child(i);
        auto data = getItemNodeData(child);
        if (data.isNode() && !data.isLoaded()) {
            loadNode(child, data.getNode(), 1);
        }
    }
}

QString NotebookNodeExplorer::generateToolTip(const Node *p_node)
{
    Q_ASSERT(p_node->isLoaded());
    QString tip = p_node->exists() ? p_node->getName() : (tr("[Invalid] %1").arg(p_node->getName()));
    tip += QLatin1String("\n\n");

    if (!p_node->getTags().isEmpty()) {
        const auto &tags = p_node->getTags();
        QString tagString = tags.first();
        for (int i = 1; i < tags.size(); ++i) {
            tagString += QLatin1String("; ") + tags[i];
        }
        tip += tr("Tags: %1\n").arg(tagString);
    }

    tip += tr("Created Time: %1\n").arg(Utils::dateTimeString(p_node->getCreatedTimeUtc().toLocalTime()));
    tip += tr("Modified Time: %1").arg(Utils::dateTimeString(p_node->getModifiedTimeUtc().toLocalTime()));
    return tip;
}
