#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QMap>
#include <QList>
#include <QHash>

#include "vtreewidget.h"
#include "vdirectory.h"
#include "vnotebook.h"
#include "vnavigationmode.h"
#include "vconstants.h"

class VEditArea;
class QLabel;

class VDirectoryTree : public VTreeWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VDirectoryTree(QWidget *parent = 0);

    void setEditArea(VEditArea *p_editArea);

    // Locate to the item representing @p_directory.
    bool locateDirectory(const VDirectory *p_directory);

    const VNotebook *currentNotebook() const;

    VDirectory *currentDirectory() const;

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;
    void reloadAllFromDisk();

signals:
    void currentDirectoryChanged(VDirectory *p_directory);

    void directoryUpdated(const VDirectory *p_directory, UpdateAction p_act);

public slots:
    // Set directory tree to display a given notebook @p_notebook.
    void setNotebook(VNotebook *p_notebook);

    // Create a root folder.
    void newRootDirectory();

    // View and edit info about directory.
    void editDirectoryInfo();

    // Clear and re-build the whole directory tree.
    // Do not load all the sub-directories at once.
    void updateDirectoryTree();

private slots:
    // Set the state of expansion of the directory.
    void handleItemExpanded(QTreeWidgetItem *p_item);

    // Set the state of expansion of the directory.
    void handleItemCollapsed(QTreeWidgetItem *p_item);

    void contextMenuRequested(QPoint pos);

    // Directory selected folder.
    // Currently only support single selected item.
    void deleteSelectedDirectory();

    // Create sub-directory of current item's directory.
    void newSubDirectory();

    void newDirectory(QTreeWidgetItem *p_parentItem);

    // Current tree item changed.
    void currentDirectoryItemChanged(QTreeWidgetItem *currentItem);

    // Copy selected directories.
    // Will put a Json string into the clipboard which contains the information
    // about copied directories.
    void copySelectedDirectories(bool p_isCut = false);

    void cutSelectedDirectories();

    // Paste directories from clipboard as sub-directories of current item.
    void pasteDirectoriesFromClipboard();

    // Open the folder's parent directory in system's file browser.
    void openDirectoryLocation() const;

    // Reload the content of current directory.
    void reloadFromDisk();

    // Sort sub-folders of current item's folder.
    void sortItems();

    // Pin selected directory to History.
    void pinDirectoryToHistory();

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

    QStringList mimeTypes() const Q_DECL_OVERRIDE;

    Qt::DropActions supportedDropActions() const Q_DECL_OVERRIDE;

    // Will be called inside dropEvent().
    bool dropMimeData(QTreeWidgetItem *p_parent,
                      int p_index,
                      const QMimeData *p_data,
                      Qt::DropAction p_action) Q_DECL_OVERRIDE;

    // Drop the data.
    void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

private:
    // Build the subtree of @p_parent recursively to the depth @p_depth.
    // @p_depth: negative - infinite levels.
    // Will expand the item if the corresponding directory was expanded before.
    // Will treat items with children as items having been built before.
    void buildSubTree(QTreeWidgetItem *p_parent, int p_depth);

    // Fill the content of a tree item according to @p_directory.
    void fillTreeItem(QTreeWidgetItem *p_item, VDirectory *p_directory);

    void initShortcuts();

    // Update @p_item's direct children only: deleted, added, renamed.
    void updateItemDirectChildren(QTreeWidgetItem *p_item);

    // Find the corresponding item of @p_dir;
    // Return's NULL if no item is found and it is the root directory if @p_widget is true.
    QTreeWidgetItem *findVDirectory(const VDirectory *p_dir, bool *p_widget = NULL);

    QPointer<VDirectory> getVDirectory(QTreeWidgetItem *p_item) const;

    // Paste @p_dirs as sub-directory of @p_destDir.
    void pasteDirectories(VDirectory *p_destDir,
                          const QVector<QString> &p_dirs,
                          bool p_isCut);

    // Build the subtree of @p_item's children if it has not been built yet.
    // We need to fill the children before showing a item to get a correct render.
    void buildChildren(QTreeWidgetItem *p_item);

    // Expand/create the directory tree nodes to @p_directory.
    QTreeWidgetItem *expandToVDirectory(const VDirectory *p_directory);

    // Expand the currently-built subtree of @p_item according to VDirectory.isExpanded().
    void expandSubTree(QTreeWidgetItem *p_item);

    // We use a map to save and restore current directory of each notebook.
    // Try to restore current directory after changing notebook.
    // Return false if no cache item found for current notebook.
    bool restoreCurrentItem();

    // Generate new magic to m_magicForClipboard.
    int getNewMagic();

    // Check if @p_magic equals to m_magicForClipboard.
    bool checkMagic(int p_magic) const;

    // Check if clipboard contains valid info to paste as directories.
    bool pasteAvailable() const;

    // Sort sub-directories of @p_dir.
    void sortItems(VDirectory *p_dir);

    QPointer<VNotebook> m_notebook;

    VEditArea *m_editArea;

    // Each notebook's current item's VDirectory.
    QHash<VNotebook *, VDirectory *> m_notebookCurrentDirMap;

    // Magic number for clipboard operations.
    int m_magicForClipboard;
};

inline QPointer<VDirectory> VDirectoryTree::getVDirectory(QTreeWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    return p_item->data(0, Qt::UserRole).value<VDirectory *>();
}

inline void VDirectoryTree::setEditArea(VEditArea *p_editArea)
{
    m_editArea = p_editArea;
}

inline const VNotebook *VDirectoryTree::currentNotebook() const
{
    return m_notebook;
}

#endif // VDIRECTORYTREE_H
