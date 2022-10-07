#ifndef NOTEBOOKNODEEXPLORER_H
#define NOTEBOOKNODEEXPLORER_H

#include <QWidget>
#include <QSharedPointer>
#include <QHash>
#include <QScopedPointer>
#include <QPair>

#include "qtreewidgetstatecache.h"
#include "clipboarddata.h"
#include "navigationmodewrapper.h"
#include "global.h"

class QSplitter;
class QMenu;

namespace vnotex
{
    class Notebook;
    class Node;
    class TreeWidget;
    class ListWidget;
    struct FileOpenParameters;
    class Event;
    class ExternalNode;

    class NotebookNodeExplorer : public QWidget
    {
        Q_OBJECT
    public:
        // Used for a QTreeWidgetItem to hold the info of a node.
        // Make it public since we need to hold it in a QTreeWidgetItem.
        class NodeData
        {
        public:
            enum class NodeType { Node, ExternalNode, Invalid };

            NodeData();

            explicit NodeData(Node *p_node, bool p_loaded);

            explicit NodeData(const QSharedPointer<ExternalNode> &p_externalNode);

            NodeData(const NodeData &p_other);

            ~NodeData();

            NodeData &operator=(const NodeData &p_other);

            bool isValid() const;

            bool isNode() const;

            bool isExternalNode() const;

            NodeData::NodeType getType() const;

            Node *getNode() const;

            // Return shared ptr to avoid wild pointer after destruction of item.
            const QSharedPointer<ExternalNode> &getExternalNode() const;

            void clear();

            bool matched(const Node *p_node) const;

            bool matched(const QString &p_name) const;

            bool isLoaded() const;

        private:
            NodeType m_type = NodeType::Invalid;

            Node *m_node = nullptr;

            QSharedPointer<ExternalNode> m_externalNode;

            bool m_loaded = false;
        };

        enum ExploreMode
        {
            Combined = 0,
            SeparateSingle,
            SeparateDouble,
            ExploreModeMax
        };

        explicit NotebookNodeExplorer(QWidget *p_parent = nullptr);

        void setNotebook(const QSharedPointer<Notebook> &p_notebook);

        void setCurrentNode(Node *p_node);

        void reload();

        void setViewOrder(int p_order);

        void setExploreMode(int p_mode);

        void setExternalFilesVisible(bool p_visible);

        void setAutoImportExternalFiles(bool p_enabled);

        Node *currentExploredFolderNode() const;

        Node *currentExploredNode() const;

        QByteArray saveState() const;

        void restoreState(const QByteArray &p_data);

        static QString generateToolTip(const Node *p_node);

    signals:
        void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

        void fileActivated(const QString &p_path, const QSharedPointer<FileOpenParameters> &p_paras);

        // @m_response of @p_event: true to continue the move, false to cancel the move.
        void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);

        // @m_response of @p_event: true to continue the removal, false to cancel the removal.
        void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);

        void nodeAboutToReload(Node *p_node, const QSharedPointer<Event> &p_event);

        // @p_filePath is either an external file or a node.
        void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

    private:
        enum Column { Name = 0 };

        enum class Action
        {
            NewNote,
            NewFolder,
            Properties,
            OpenLocation,
            CopyPath,
            Copy,
            Cut,
            Paste,
            Delete,
            RemoveFromConfig,
            Sort,
            Reload,
            ReloadIndex,
            ImportToConfig,
            Open,
            Edit,
            Read,
            ExpandAll,
            PinToQuickAccess,
            Tag
        };

        struct CacheData
        {
            void clear();

            QSharedPointer<QTreeWidgetStateCache<Node *>> m_masterStateCache;

            QString m_currentSlaveName;
        };

        void setupUI();

        bool isActionFromMaster() const;

        void setupShortcuts();

        void setupMasterExplorer(QWidget *p_parent = nullptr);

        void setupSlaveExplorer();

        void generateMasterNodeTree();

        void loadRootNode(const Node *p_node) const;

        void loadMasterNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadMasterNodeChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadMasterItemChildren(QTreeWidgetItem *p_item) const;

        void loadMasterExternalNode(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const;

        void fillMasterItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const;

        void fillMasterItem(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const;

        void fillSlaveItem(QListWidgetItem *p_item, Node *p_node) const;

        void fillSlaveItem(QListWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const;

        const QIcon &getIcon(const Node *p_node) const;

        const QIcon &getIcon(const ExternalNode *p_node) const;

        void initNodeIcons() const;

        QTreeWidgetItem *findMasterNode(const Node *p_node) const;

        QTreeWidgetItem *findMasterNode(QTreeWidgetItem *p_item, const Node *p_node) const;

        QTreeWidgetItem *findMasterNodeInDirectChildren(QTreeWidgetItem *p_item, const Node *p_node) const;

        QTreeWidgetItem *findMasterNodeInTopLevelItems(QTreeWidget *p_tree, const Node *p_node) const;

        QListWidgetItem *findSlaveNode(const Node *p_node) const;

        void cacheState(bool p_saveCurrent);

        // Get cache data of current notebook.
        CacheData &getCache() const;

        void clearCache(const Notebook *p_notebook);

        void createMasterContextMenuOnRoot(QMenu *p_menu);

        void createContextMenuOnNode(QMenu *p_menu, const Node *p_node, bool p_master);

        void createContextMenuOnExternalNode(QMenu *p_menu, const ExternalNode *p_node, bool p_master);

        void createSlaveContextMenuOnMasterNode(QMenu *p_menu);

        // Factory function to create action.
        QAction *createAction(Action p_act, QObject *p_parent, bool p_master);

        QAction *createAndAddAction(Action p_act, QMenu *p_menu, bool p_master = true);

        void copySelectedNodes(bool p_move, bool p_master);

        void pasteNodesFromClipboard();

        QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> getMasterSelectedNodesAndExternalNodes() const;

        QPair<QVector<Node *>, QVector<QSharedPointer<ExternalNode>>> getSlaveSelectedNodesAndExternalNodes() const;

        void removeSelectedNodes(bool p_master);

        void removeSelectedNodesFromConfig(bool p_master);

        QVector<Node *> confirmSelectedNodes(const QString &p_title,
                                             const QString &p_text,
                                             const QString &p_info,
                                             bool p_master) const;

        static QSharedPointer<ClipboardData> tryFetchClipboardData();

        bool isPasteOnNodeAvailable(const Node *p_node) const;

        void setMasterNodeExpanded(const Node *p_node, bool p_expanded);

        // Select both master and slave nodes.
        void selectNodes(const QVector<const Node *> &p_nodes);

        void removeNodes(QVector<Node *> p_nodes, bool p_configOnly);

        void filterAwayChildrenNodes(QVector<Node *> &p_nodes);

        void updateAndExpandNode(Node *p_node);

        // Check if all selected items are the same type for operations.
        bool isMasterAllSelectedItemsSameType() const;

        bool isSlaveAllSelectedItemsSameType() const;

        void focusNormalNode();

        void sortNodes(QVector<QSharedPointer<Node>> &p_nodes) const;

        // [p_start, p_end).
        void sortNodes(QVector<QSharedPointer<Node>> &p_nodes, int p_start, int p_end, ViewOrder p_viewOrder) const;

        // Sort nodes in config file.
        void manualSort(bool p_master);

        QSharedPointer<Node> importToIndex(QSharedPointer<ExternalNode> p_node);

        void importToIndex(const QVector<QSharedPointer<ExternalNode>> &p_nodes);

        // Check whether @p_node is a valid node. Will notify user.
        // Return true if it is invalid.
        bool checkInvalidNode(Node *p_node) const;

        void addOpenWithMenu(QMenu *p_menu, bool p_master);

        QStringList getSelectedNodesPath(bool p_master) const;

        void openSelectedNodesWithProgram(const QString &p_name, bool p_master);

        void openCurrentNodeProperties(bool p_master);

        bool belongsToMasterExplorer(const Node *p_node) const;

        bool belongsToMasterExplorer(const ExternalNode *p_node) const;

        void updateSlaveExplorer();

        Node *getCurrentMasterNode() const;

        Node *getCurrentSlaveNode() const;

        NodeData getCurrentMasterNodeData() const;

        NodeData getCurrentSlaveNodeData() const;

        Node *getSlaveExplorerMasterNode() const;

        bool isCombinedExploreMode() const;

        // Update the tree of @p_node if there is any. Or update the node itself if it is in slave explorer.
        // If @p_node is null, update the whole tree.
        void updateNode(Node *p_node);

        void setCurrentMasterNode(Node *p_node);

        void setCurrentSlaveNode(const Node *p_node);

        void setCurrentSlaveNode(const QString &p_name);

        void activateItemNode(const NodeData &p_data);

        static NotebookNodeExplorer::NodeData getItemNodeData(const QTreeWidgetItem *p_item);

        static NotebookNodeExplorer::NodeData getItemNodeData(const QListWidgetItem *p_item);

        static void setItemNodeData(QTreeWidgetItem *p_item, const NodeData &p_data);

        static void setItemNodeData(QListWidgetItem *p_item, const NodeData &p_data);

        QSplitter *m_splitter = nullptr;

        TreeWidget *m_masterExplorer = nullptr;

        ListWidget *m_slaveExplorer = nullptr;

        QSharedPointer<Notebook> m_notebook;

        QHash<const Notebook *, CacheData> m_cache;

        QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_masterNavigationWrapper;

        QScopedPointer<NavigationModeWrapper<QListWidget, QListWidgetItem>> m_slaveNavigationWrapper;

        ViewOrder m_viewOrder = ViewOrder::OrderedByConfiguration;

        ExploreMode m_exploreMode = ExploreMode::Combined;

        bool m_externalFilesVisible = true;

        bool m_autoImportExternalFiles = true;

        enum NodeIcon
        {
            FolderNode = 0,
            FileNode,
            InvalidFolderNode,
            InvalidFileNode,
            ExternalFolderNode,
            ExternalFileNode,
            MaxIcons
        };

        static QIcon s_nodeIcons[NodeIcon::MaxIcons];
    };
}


Q_DECLARE_METATYPE(vnotex::NotebookNodeExplorer::NodeData);

#endif // NOTEBOOKNODEEXPLORER_H
