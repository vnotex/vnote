#include "notebooknodeexplorer.h"

#include <QtWidgets>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include "exception.h"
#include "messageboxhelper.h"
#include "vnotex.h"
#include "mainwindow.h"
#include <utils/iconutils.h>
#include "treewidget.h"
#include "dialogs/notepropertiesdialog.h"
#include "dialogs/folderpropertiesdialog.h"
#include "dialogs/deleteconfirmdialog.h"
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include <utils/clipboardutils.h>
#include "notebookmgr.h"
#include "widgetsfactory.h"
#include "navigationmodemgr.h"

#include <core/fileopenparameters.h>
#include <core/events.h>

using namespace vnotex;

const QString NotebookNodeExplorer::c_nodeIconForegroundName = "widgets#notebookexplorer#node_icon#fg";

QIcon NotebookNodeExplorer::s_folderNodeIcon;

QIcon NotebookNodeExplorer::s_fileNodeIcon;

QIcon NotebookNodeExplorer::s_recycleBinNodeIcon;

NotebookNodeExplorer::NodeData::NodeData()
{
}

NotebookNodeExplorer::NodeData::NodeData(Node *p_node, bool p_loaded)
    : m_type(NodeType::Node),
      m_node(p_node),
      m_loaded(p_loaded)
{
}

NotebookNodeExplorer::NodeData::NodeData(const QString &p_name)
    : m_type(NodeType::Attachment),
      m_name(p_name),
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

    case NodeType::Attachment:
        m_name = p_other.m_name;
        break;

    default:
        m_node = p_other.m_node;
        m_name = p_other.m_name;
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

    case NodeType::Attachment:
        m_name = p_other.m_name;
        break;

    default:
        m_node = p_other.m_node;
        m_name = p_other.m_name;
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

bool NotebookNodeExplorer::NodeData::isAttachment() const
{
    return m_type == NodeType::Attachment;
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

const QString &NotebookNodeExplorer::NodeData::getName() const
{
    Q_ASSERT(isAttachment());
    return m_name;
}

void NotebookNodeExplorer::NodeData::clear()
{
    m_type = NodeType::Invalid;
    m_node = nullptr;
    m_name.clear();
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

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(c_nodeIconForegroundName);
    const QString folderIconName("folder_node.svg");
    const QString fileIconName("file_node.svg");
    const QString recycleBinIconName("recycle_bin.svg");

    s_folderNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(folderIconName), fg);
    s_fileNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(fileIconName), fg);
    s_recycleBinNodeIcon = IconUtils::fetchIcon(themeMgr.getIconFile(recycleBinIconName), fg);
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
                    } else if (data.isAttachment()) {
                        createContextMenuOnAttachment(menu.data(), data.getName());
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
                    emit nodeActivated(data.getNode(), QSharedPointer<FileOpenParameters>::create());
                } else if (data.isAttachment()) {
                    // TODO.
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
    Q_ASSERT(p_node->isLoaded());

    // Render recycle bin node first.
    auto recycleBinNode = m_notebook->getRecycleBinNode();
    if (recycleBinNode) {
        loadRecycleBinNode(recycleBinNode.data());
    }

    for (auto &child : p_node->getChildren()) {
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

void NotebookNodeExplorer::loadChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const
{
    if (p_level < 0) {
        return;
    }

    for (auto &child : p_node->getChildren()) {
        auto item = new QTreeWidgetItem(p_item);
        loadNode(item, child.data(), p_level);
    }
}

void NotebookNodeExplorer::loadRecycleBinNode(Node *p_node) const
{
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
    p_item->setToolTip(Column::Name, p_node->getName());
}

QIcon NotebookNodeExplorer::getNodeItemIcon(const Node *p_node) const
{
    if (p_node->hasContent()) {
        return s_fileNodeIcon;
    } else {
        if (p_node->getUse() == Node::Use::RecycleBin) {
            return s_recycleBinNodeIcon;
        }

        return s_folderNodeIcon;
    }

    return QIcon();
}

Node *NotebookNodeExplorer::getCurrentNode() const
{
    auto item = m_masterExplorer->currentItem();
    if (item) {
        auto data = getItemNodeData(item);
        while (data.isAttachment()) {
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

    for (auto &it : items) {
        it->setExpanded(true);
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

    act = createAction(Action::OpenLocation, p_menu);
    p_menu->addAction(act);
}

void NotebookNodeExplorer::createContextMenuOnNode(QMenu *p_menu, const Node *p_node)
{
    const int selectedSize = m_masterExplorer->selectedItems().size();
    QAction *act = nullptr;

    if (m_notebook->isRecycleBinNode(p_node)) {
        // Recycle bin node.
        if (selectedSize == 1) {
            act = createAction(Action::EmptyRecycleBin, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::OpenLocation, p_menu);
            p_menu->addAction(act);
        }
    } else if (m_notebook->isNodeInRecycleBin(p_node)) {
        // Node in recycle bin.
        act = createAction(Action::Cut, p_menu);
        p_menu->addAction(act);

        act = createAction(Action::DeleteFromRecycleBin, p_menu);
        p_menu->addAction(act);

        if (selectedSize == 1) {
            p_menu->addSeparator();

            act = createAction(Action::CopyPath, p_menu);
            p_menu->addAction(act);

            act = createAction(Action::OpenLocation, p_menu);
            p_menu->addAction(act);
        }
    } else {
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

void NotebookNodeExplorer::createContextMenuOnAttachment(QMenu *p_menu, const QString &p_name)
{
    Q_UNUSED(p_menu);
    Q_UNUSED(p_name);
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
                    if (!node) {
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
                    QString locationPath;
                    auto node = getCurrentNode();
                    if (node) {
                        locationPath = node->fetchAbsolutePath();
                        if (!node->isContainer()) {
                            locationPath = PathUtils::parentDirPath(locationPath);
                        }
                    } else if (m_notebook) {
                        locationPath = m_notebook->getRootFolderAbsolutePath();
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
                    auto node = getCurrentNode();
                    if (node) {
                        auto nodePath = node->fetchAbsolutePath();
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
                                                 tr("Failed to empty recycle bin (%1) (%2).")
                                                   .arg(rbNodePath, p_e.what()),
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
        act = new QAction(tr("&Delete From Recycle Bin"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    removeSelectedNodes(true);
                });
        break;

    case Action::RemoveFromConfig:
        act = new QAction(tr("&Remove From Index"), p_parent);
        connect(act, &QAction::triggered,
                this, [this]() {
                    removeSelectedNodesFromConfig();
                });
        break;
    }

    return act;
}

void NotebookNodeExplorer::copySelectedNodes(bool p_move)
{
    auto nodes = getSelectedNodes();
    if (nodes.isEmpty()) {
        return;
    }

    filterAwayChildrenNodes(nodes);

    ClipboardData cdata(VNoteX::getInst().getInstanceId(),
                        p_move ? ClipboardData::MoveNode : ClipboardData::CopyNode);
    for (auto node : nodes) {
        auto item = QSharedPointer<NodeClipboardDataItem>::create(node->getNotebook()->getId(),
                                                                  node->fetchPath());
        cdata.addItem(item);
    }

    auto text = cdata.toJsonText();
    ClipboardUtils::setTextToClipboard(text);

    size_t nrItems = cdata.getData().size();
    VNoteX::getInst().showStatusMessageShort(tr("Copied %n item(s)", "", static_cast<int>(nrItems)));
}

QVector<Node *> NotebookNodeExplorer::getSelectedNodes() const
{
    QVector<Node *> nodes;

    auto items = m_masterExplorer->selectedItems();
    for (auto &item : items) {
        auto data = getItemNodeData(item);
        if (data.isNode()) {
            nodes.push_back(data.getNode());
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
    auto nodes = getSelectedNodes();
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

    if (!p_configOnly && !p_skipRecycleBin) {
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
    for (int i = p_nodes.size() - 1; i > 0; --i) {
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
    if (item && item != m_masterExplorer->topLevelItem(0)) {
        // Not recycle bin.
        return;
    }

    auto cnt = m_masterExplorer->topLevelItemCount();
    if (cnt > 1) {
        m_masterExplorer->setCurrentItem(m_masterExplorer->topLevelItem(1));
    }
}
