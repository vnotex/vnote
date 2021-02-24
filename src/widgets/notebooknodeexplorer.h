#ifndef NOTEBOOKNODEEXPLORER_H
#define NOTEBOOKNODEEXPLORER_H

#include <QWidget>
#include <QSharedPointer>
#include <QHash>
#include <QScopedPointer>

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

    class NotebookNodeExplorer : public QWidget
    {
        Q_OBJECT
    public:
        // Used for a QTreeWidgetItem to hold the info of a node.
        // Make it public since we need to hold it in a QTreeWidgetItem.
        class NodeData
        {
        public:
            enum class NodeType { Node, Attachment, Invalid };

            NodeData();

            explicit NodeData(Node *p_node, bool p_loaded);

            explicit NodeData(const QString &p_name);

            NodeData(const NodeData &p_other);

            ~NodeData();

            NodeData &operator=(const NodeData &p_other);

            bool isValid() const;

            bool isNode() const;

            bool isAttachment() const;

            NodeData::NodeType getType() const;

            Node *getNode() const;

            const QString &getName() const;

            void clear();

            bool matched(const Node *p_node) const;

            bool isLoaded() const;

        private:
            NodeType m_type = NodeType::Invalid;

            union
            {
                Node *m_node = nullptr;
                QString m_name;
            };

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

    signals:
        void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

        void fileActivated(const QString &p_path);

        // @m_response of @p_event: true to continue the move, false to cancel the move.
        void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);

        // @m_response of @p_event: true to continue the removal, false to cancel the removal.
        void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);

    private:
        enum Column { Name = 0 };

        enum Action { NewNote, NewFolder, Properties, OpenLocation, CopyPath,
                      Copy, Cut, Paste, EmptyRecycleBin, Delete,
                      DeleteFromRecycleBin, RemoveFromConfig };

        void setupUI();

        void setupMasterExplorer(QWidget *p_parent = nullptr);

        void clearExplorer();

        void generateNodeTree();

        void loadRootNode(const Node *p_node) const;

        void loadNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadChildren(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void loadRecycleBinNode(Node *p_node) const;

        void loadRecycleBinNode(QTreeWidgetItem *p_item, Node *p_node, int p_level) const;

        void fillTreeItem(QTreeWidgetItem *p_item, Node *p_node, bool p_loaded) const;

        QIcon getNodeItemIcon(const Node *p_node) const;

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

        void createContextMenuOnAttachment(QMenu *p_menu, const QString &p_name);

        // Factory function to create action.
        QAction *createAction(Action p_act, QObject *p_parent);

        void copySelectedNodes(bool p_move);

        void pasteNodesFromClipboard();

        // Only return selected Nodes.
        QVector<Node *> getSelectedNodes() const;

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

        static NotebookNodeExplorer::NodeData getItemNodeData(const QTreeWidgetItem *p_item);

        static void setItemNodeData(QTreeWidgetItem *p_item, const NodeData &p_data);

        QSplitter *m_splitter = nullptr;

        TreeWidget *m_masterExplorer = nullptr;

        QSharedPointer<Notebook> m_notebook;

        QHash<const Notebook *, QSharedPointer<QTreeWidgetStateCache<Node *>>> m_stateCache;

        QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_navigationWrapper;

        static QIcon s_folderNodeIcon;
        static QIcon s_fileNodeIcon;
        static QIcon s_recycleBinNodeIcon;

        static const QString c_nodeIconForegroundName;
    };
}


Q_DECLARE_METATYPE(vnotex::NotebookNodeExplorer::NodeData);

#endif // NOTEBOOKNODEEXPLORER_H
