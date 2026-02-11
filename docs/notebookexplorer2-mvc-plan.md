# NotebookExplorer2 MVC Architecture Plan

## Overview

This document describes the redesign of `NotebookExplorer` using proper Model-View-Controller (MVC) architecture. The new implementation will be named `NotebookExplorer2` and coexist with the original implementation to allow gradual migration.

## Goals

1. **Clean MVC separation** - Model handles data, View handles display, Controller handles user actions
2. **Preserve all functionality** - Every feature from current NotebookExplorer must work
3. **Simplified explore modes** - Only Combined and TwoColumns views (removed Tree/VerticalLine/Horizontal modes)
4. **Use existing domain classes** - Leverage `Notebook` and `Node` classes for data access
5. **Coexist with legacy** - New classes use suffix `2` to avoid naming conflicts

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    NotebookExplorer2                        │
│  (Container Widget - src/widgets/)                          │
│  ┌──────────────┐ ┌──────────────────────────────────────┐  │
│  │  TitleBar    │ │        Content Area                  │  │
│  │  (existing)  │ │  ┌────────────────────────────────┐  │  │
│  └──────────────┘ │  │  Combined: NotebookNodeView    │  │  │
│  ┌──────────────┐ │  │  -or-                          │  │  │
│  │  Notebook    │ │  │  TwoColumns:                   │  │  │
│  │  Selector    │ │  │    [FolderView | FileView]     │  │  │
│  │  (existing)  │ │  └────────────────────────────────┘  │  │
│  └──────────────┘ └──────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │                        │
         │                        │
         ▼                        ▼
┌─────────────────┐    ┌─────────────────────┐
│ NotebookNode    │◄───│ NotebookNodeView    │
│ Controller      │    │ (src/views/)        │
│ (src/controllers│    │                     │
│ /)              │    │ Uses:               │
│                 │    │ - NotebookNodeModel │
│ Handles:        │    │ - NotebookNodeProxy │
│ - Context menus │    │ - NotebookNode      │
│ - Drag & drop   │    │   Delegate          │
│ - Keyboard      │    └─────────────────────┘
│ - All actions   │              │
└─────────────────┘              │
         │                       ▼
         │            ┌─────────────────────┐
         │            │ NotebookNodeModel   │
         └───────────►│ (src/models/)       │
                      │                     │
                      │ Wraps:              │
                      │ - Notebook          │
                      │ - Node hierarchy    │
                      └─────────────────────┘
```

## Directory Structure

```
src/
├── models/
│   ├── CMakeLists.txt
│   ├── notebooknodemodel.h
│   ├── notebooknodemodel.cpp
│   ├── notebooknodeproxymodel.h
│   └── notebooknodeproxymodel.cpp
├── views/
│   ├── CMakeLists.txt
│   ├── notebooknodeview.h
│   ├── notebooknodeview.cpp
│   ├── notebooknodedelegate.h
│   └── notebooknodedelegate.cpp
├── controllers/
│   ├── CMakeLists.txt
│   ├── notebooknodecontroller.h
│   └── notebooknodecontroller.cpp
└── widgets/
    ├── notebookexplorer2.h      (NEW)
    ├── notebookexplorer2.cpp    (NEW)
    └── ... (existing files unchanged)
```

## Component Details

### 1. Model Layer (src/models/)

#### NotebookNodeModel

A `QAbstractItemModel` implementation that exposes `Notebook`/`Node` hierarchy to Qt's Model/View framework.

```cpp
class NotebookNodeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Roles {
        NodeRole = Qt::UserRole + 1,     // Node* pointer
        NodeTypeRole,                     // Node::Type
        NodeFlagsRole,                    // Node::Flags
        IsContainerRole,                  // bool - is folder
        ChildCountRole,                   // int - number of children
        PathRole,                         // QString - node path
        ModifiedTimeRole                  // QDateTime
    };

    explicit NotebookNodeModel(QObject *parent = nullptr);

    // Core QAbstractItemModel interface
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Lazy loading
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    // Drag & drop
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                      int row, int column, const QModelIndex &parent) override;

    // Custom API
    void setNotebook(const QSharedPointer<Notebook> &notebook);
    QSharedPointer<Notebook> getNotebook() const;
    Node *nodeFromIndex(const QModelIndex &index) const;
    QModelIndex indexFromNode(Node *node) const;
    void reload();
    void reloadNode(Node *node);

signals:
    void notebookChanged();

private:
    QSharedPointer<Notebook> m_notebook;
    // Internal node state tracking for lazy loading
    mutable QSet<Node*> m_fetchedNodes;
};
```

#### NotebookNodeProxyModel

A `QSortFilterProxyModel` for sorting and filtering nodes.

```cpp
class NotebookNodeProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    enum FilterFlag {
        ShowFolders = 0x01,
        ShowNotes = 0x02,
        ShowAttachments = 0x04,
        ShowHidden = 0x08,
        ShowAll = ShowFolders | ShowNotes | ShowAttachments
    };
    Q_DECLARE_FLAGS(FilterFlags, FilterFlag)

    explicit NotebookNodeProxyModel(QObject *parent = nullptr);

    void setFilterFlags(FilterFlags flags);
    FilterFlags filterFlags() const;

    void setNameFilter(const QString &pattern);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    FilterFlags m_filterFlags = ShowAll;
    QString m_nameFilter;
};
```

### 2. View Layer (src/views/)

#### NotebookNodeView

A `QTreeView` subclass that handles display and basic interaction.

```cpp
class NotebookNodeView : public QTreeView {
    Q_OBJECT

public:
    explicit NotebookNodeView(QWidget *parent = nullptr);

    void setController(NotebookNodeController *controller);

    // Selection helpers
    Node *currentNode() const;
    QList<Node*> selectedNodes() const;
    void selectNode(Node *node);

    // Navigation
    void expandToNode(Node *node);
    void scrollToNode(Node *node);

signals:
    void nodeActivated(Node *node);
    void selectionChanged(const QList<Node*> &nodes);
    void contextMenuRequested(Node *node, const QPoint &globalPos);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    NotebookNodeController *m_controller = nullptr;
};
```

#### NotebookNodeDelegate

A `QStyledItemDelegate` for custom node rendering.

```cpp
class NotebookNodeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit NotebookNodeDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    void paintNode(QPainter *painter, const QStyleOptionViewItem &option,
                   Node *node) const;
    QIcon iconForNode(Node *node) const;
};
```

### 3. Controller Layer (src/controllers/)

#### NotebookNodeController

Handles all user actions and business logic.

```cpp
class NotebookNodeController : public QObject {
    Q_OBJECT

public:
    explicit NotebookNodeController(QObject *parent = nullptr);

    void setModel(NotebookNodeModel *model);
    void setView(NotebookNodeView *view);

    // Context menu
    QMenu *createContextMenu(Node *node);

    // Node operations (matching current NotebookNodeExplorer functionality)
    void newNote(Node *parentNode);
    void newFolder(Node *parentNode);
    void openNode(Node *node);
    void openNodeWith(Node *node);
    void deleteNodes(const QList<Node*> &nodes);
    void removeNodes(const QList<Node*> &nodes);  // Remove from notebook only
    void copyNodes(const QList<Node*> &nodes);
    void cutNodes(const QList<Node*> &nodes);
    void pasteNodes(Node *targetFolder);
    void duplicateNode(Node *node);
    void renameNode(Node *node);
    void moveNodes(const QList<Node*> &nodes, Node *targetFolder);

    // Import/Export
    void importFile(Node *targetFolder);
    void importFolder(Node *targetFolder);
    void exportNode(Node *node);

    // Properties and info
    void showNodeProperties(Node *node);
    void copyNodePath(Node *node);
    void locateNodeInExplorer(Node *node);

    // Sorting
    void sortNodes(Node *parentNode);
    void reloadNode(Node *node);
    void reloadAll();

    // Pin/Tag operations
    void pinNodeToQuickAccess(Node *node);
    void manageNodeTags(Node *node);

signals:
    // Signals to notify external components
    void nodeActivated(Node *node, const QSharedPointer<FileOpenParameters> &params);
    void fileActivated(const QString &path, const QSharedPointer<FileOpenParameters> &params);
    void nodeAboutToMove(Node *node, const QSharedPointer<Event> &event);
    void nodeAboutToRemove(Node *node, const QSharedPointer<Event> &event);
    void nodeAboutToReload(Node *node, const QSharedPointer<Event> &event);
    void closeFileRequested(const QString &filePath, const QSharedPointer<Event> &event);

private:
    NotebookNodeModel *m_model = nullptr;
    NotebookNodeView *m_view = nullptr;

    // Clipboard state
    QList<Node*> m_clipboardNodes;
    bool m_isCut = false;

    // Helper methods
    bool confirmDelete(const QList<Node*> &nodes);
    void notifyBufferBeforeOperation(Node *node, const QString &operation);
};
```

### 4. Container Widget (src/widgets/)

#### NotebookExplorer2

The main container that assembles all components.

```cpp
class NotebookExplorer2 : public QFrame {
    Q_OBJECT

public:
    enum ExploreMode {
        Combined,      // Single tree with folders and files
        TwoColumns     // Left: folders, Right: files
    };

    explicit NotebookExplorer2(QWidget *parent = nullptr);

    // Notebook management
    void setCurrentNotebook(const QSharedPointer<Notebook> &notebook);
    QSharedPointer<Notebook> currentNotebook() const;

    // Navigation
    void setCurrentNode(Node *node);
    Node *currentNode() const;

    // Mode switching
    void setExploreMode(ExploreMode mode);
    ExploreMode exploreMode() const;

signals:
    // Forwarded from controller
    void nodeActivated(Node *node, const QSharedPointer<FileOpenParameters> &params);
    void fileActivated(const QString &path, const QSharedPointer<FileOpenParameters> &params);
    void nodeAboutToMove(Node *node, const QSharedPointer<Event> &event);
    void nodeAboutToRemove(Node *node, const QSharedPointer<Event> &event);
    void nodeAboutToReload(Node *node, const QSharedPointer<Event> &event);
    void closeFileRequested(const QString &filePath, const QSharedPointer<Event> &event);

private slots:
    void onNotebookChanged(int index);
    void onNodeActivated(Node *node);

private:
    void setupUI();
    void setupCombinedView();
    void setupTwoColumnsView();
    void connectSignals();

    // UI Components
    TitleBar *m_titleBar = nullptr;
    NotebookSelector *m_notebookSelector = nullptr;
    QStackedWidget *m_contentStack = nullptr;

    // MVC Components - Combined mode
    NotebookNodeModel *m_model = nullptr;
    NotebookNodeProxyModel *m_proxyModel = nullptr;
    NotebookNodeView *m_combinedView = nullptr;
    NotebookNodeDelegate *m_delegate = nullptr;
    NotebookNodeController *m_controller = nullptr;

    // MVC Components - TwoColumns mode
    NotebookNodeModel *m_folderModel = nullptr;
    NotebookNodeProxyModel *m_folderProxyModel = nullptr;
    NotebookNodeView *m_folderView = nullptr;

    NotebookNodeModel *m_fileModel = nullptr;
    NotebookNodeProxyModel *m_fileProxyModel = nullptr;
    NotebookNodeView *m_fileView = nullptr;

    QSplitter *m_twoColumnsSplitter = nullptr;

    ExploreMode m_exploreMode = Combined;
};
```

## Features to Preserve

All context menu actions from current implementation must work:

### File/Folder Operations
- [x] New Note
- [x] New Folder
- [x] Open (activate node)
- [x] Open With (external app)
- [x] Delete (move to recycle bin / permanent)
- [x] Remove from Notebook (keeps file on disk)
- [x] Copy
- [x] Cut
- [x] Paste
- [x] Duplicate
- [x] Rename

### Import/Export
- [x] Import File to Folder
- [x] Import Folder to Folder
- [x] Export Node

### Navigation & Info
- [x] Copy Path
- [x] Open Location (in system file manager)
- [x] Properties Dialog
- [x] Expand All / Collapse All

### Organization
- [x] Sort (by name, modified time, etc.)
- [x] Pin to Quick Access
- [x] Manage Tags

### View Operations
- [x] Reload
- [x] Reload Index (from notebook)

## Implementation Phases

### Phase 1: Model Layer
1. Create `src/models/` directory structure
2. Implement `NotebookNodeModel` with core QAbstractItemModel methods
3. Implement lazy loading for large notebooks
4. Implement `NotebookNodeProxyModel` for filtering

### Phase 2: View Layer
1. Create `src/views/` directory structure
2. Implement `NotebookNodeDelegate` for custom painting
3. Implement `NotebookNodeView` with selection and navigation

### Phase 3: Controller Layer
1. Create `src/controllers/` directory structure
2. Implement `NotebookNodeController` with all actions
3. Implement context menu generation
4. Wire up signals for external notification

### Phase 4: Container Widget
1. Create `NotebookExplorer2` in `src/widgets/`
2. Wire up Model + View + Controller
3. Implement Combined mode
4. Implement TwoColumns mode

### Phase 5: Integration
1. Update CMakeLists.txt files
2. Build and fix compilation issues
3. Test all functionality

## Migration Strategy

1. **Coexistence** - `NotebookExplorer2` lives alongside `NotebookExplorer`
2. **Feature parity** - Ensure all features work before switching
3. **Configuration flag** - Add user preference to choose explorer
4. **Gradual deprecation** - Eventually remove old implementation

## Testing Checklist

- [ ] Model correctly reflects Notebook/Node hierarchy
- [ ] Lazy loading works for large notebooks
- [ ] All context menu actions function correctly
- [ ] Drag & drop between folders works
- [ ] External file drop import works
- [ ] Combined view displays correctly
- [ ] TwoColumns view syncs folder selection to file list
- [ ] Signals properly notify BufferMgr before operations
- [ ] Theme/icons render correctly via delegate
