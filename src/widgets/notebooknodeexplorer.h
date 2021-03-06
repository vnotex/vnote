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

class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QMenu;

namespace vnotex
{
    class Notebook;
    class Node;
    class TreeWidget;
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

            ExternalNode *getExternalNode() const;

            void clear();

            bool matched(const Node *p_node) const;

            bool isLoaded() const;

        private:
            NodeType m_type = NodeType::Invalid;

            Node *m_node = nullptr;

            QSharedPointer<ExternalNode> m_externalNode;

            bool m_loaded = false;
        };

        enum ViewOrder
        {
            OrderedByConfiguration = 0,
            OrderedByName,
            OrderedByNameReversed,
            OrderedByCreatedTime,
            OrderedByCreatedTimeReversed,
            OrderedByModifiedTime,
            OrderedByModifiedTimeReversed,
            ViewOrderMax
        };

        explicit NotebookNodeExplorer(QWidget *p_parent = nullptr);

        void setNotebook(const QSharedPointer<Notebook> &p_notebook);

        Node *getCurrentNode() const;

        // Update the tree of @p_node.
        // If @p_node is null, update the whole tree.
        void updateNode(Node *p_node);

        void setCurrentNode(Node *p_node);

        void reload();

        void setRecycleBinNodeVisible(bool p_visible);

        void setViewOrder(int p_order);

        void setExternalFilesVisible(bool p_visible);

        void setAutoImportExternalFiles(bool p_enabled);

        Node *currentExploredFolderNode() const;

    signals:
        void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

        void fileActivated(const QString &p_path, const QSharedPointer<FileOpenParameters> &p_paras);

        // @m_response of @p_event: true to continue the move, false to cancel the move.
        void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);

        // @m_response of @p_event: true to continue the removal, false to cancel the removal.
        void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);

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
            EmptyRecycleBin,
            Delete,
            DeleteFromRecycleBin,
            RemoveFromConfig,
            Sort,
            Reload,
            ImportToConfig,
            Open
        };

        void setupUI();

        void setupMasterExplorer(QWidget *p_parent = nullptr);

        void clearExplorer();

        void generateNodeTree();

        void loadRootNode(const Node *p_node) const;

        void loadNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadNode(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const;

        void loadRecycleBinNode(Node *p_node) const;

        void loadRecycleBinNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void fillTreeItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const;

        void fillTreeItem(QTreeWidgetItem *p_item, const QSharedPointer<ExternalNode> &p_node) const;

        const QIcon &getNodeItemIcon(const Node *p_node) const;

        const QIcon &getNodeItemIcon(const ExternalNode *p_node) const;

        void initNodeIcons() const;

        QTreeWidgetItem *findNode(const Node *p_node) const;

        QTreeWidgetItem *findNode(QTreeWidgetItem *p_item, const Node *p_node) const;

        QTreeWidgetItem *findNodeChild(QTreeWidgetItem *p_item, const Node *p_node) const;

        QTreeWidgetItem *findNodeTopLevelItem(QTreeWidget *p_tree, const Node *p_node) const;

        void saveNotebookTreeState(bool p_saveCurrentItem = true);

        QSharedPointer<QTreeWidgetStateCache<Node *>> stateCache() const;

        void clearStateCache(const Notebook *p_notebook);

        void createContextMenuOnRoot(QMenu *p_menu);

        void createContextMenuOnNode(QMenu *p_menu, const Node *p_node);

        void createContextMenuOnExternalNode(QMenu *p_menu, const ExternalNode *p_node);

        // Factory function to create action.
        QAction *createAction(Action p_act, QObject *p_parent);

        void copySelectedNodes(bool p_move);

        void pasteNodesFromClipboard();

        QPair<QVector<Node *>, QVector<ExternalNode *>> getSelectedNodes() const;

        void removeSelectedNodes(bool p_skipRecycleBin);

        void removeSelectedNodesFromConfig();

        QVector<Node *> confirmSelectedNodes(const QString &p_title,
                                             const QString &p_text,
                                             const QString &p_info) const;

        static QSharedPointer<ClipboardData> tryFetchClipboardData();

        bool isPasteOnNodeAvailable(const Node *p_node) const;

        void setNodeExpanded(const Node *p_node, bool p_expanded);

        void selectNodes(const QVector<const Node *> &p_nodes);

        // @p_skipRecycleBin is irrelevant if @p_configOnly is true.
        void removeNodes(QVector<Node *> p_nodes, bool p_skipRecycleBin, bool p_configOnly);

        void filterAwayChildrenNodes(QVector<Node *> &p_nodes);

        void updateAndExpandNode(Node *p_node);

        // Check if all selected items are the same type for operations.
        bool allSelectedItemsSameType() const;

        // Skip the recycle bin node if possible.
        void focusNormalNode();

        void sortNodes(QVector<QSharedPointer<Node>> &p_nodes) const;

        // [p_start, p_end).
        void sortNodes(QVector<QSharedPointer<Node>> &p_nodes, int p_start, int p_end, int p_viewOrder) const;

        // Sort nodes in config file.
        void manualSort();

        void openSelectedNodes();

        QSharedPointer<Node> importToIndex(const ExternalNode *p_node);

        void importToIndex(const QVector<ExternalNode *> &p_nodes);

        // Check whether @p_node is a valid node. Will notify user.
        // Return true if it is invalid.
        bool checkInvalidNode(const Node *p_node) const;

        static NotebookNodeExplorer::NodeData getItemNodeData(const QTreeWidgetItem *p_item);

        static void setItemNodeData(QTreeWidgetItem *p_item, const NodeData &p_data);

        QSplitter *m_splitter = nullptr;

        TreeWidget *m_masterExplorer = nullptr;

        QSharedPointer<Notebook> m_notebook;

        QHash<const Notebook *, QSharedPointer<QTreeWidgetStateCache<Node *>>> m_stateCache;

        QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_navigationWrapper;

        bool m_recycleBinNodeVisible = false;

        int m_viewOrder = ViewOrder::OrderedByConfiguration;

        bool m_externalFilesVisible = true;

        bool m_autoImportExternalFiles = true;

        static QIcon s_folderNodeIcon;

        static QIcon s_fileNodeIcon;

        static QIcon s_invalidFolderNodeIcon;

        static QIcon s_invalidFileNodeIcon;

        static QIcon s_recycleBinNodeIcon;

        static QIcon s_externalFolderNodeIcon;

        static QIcon s_externalFileNodeIcon;
    };
}


Q_DECLARE_METATYPE(vnotex::NotebookNodeExplorer::NodeData);

#endif // NOTEBOOKNODEEXPLORER_H
