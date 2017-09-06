#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QMap>
#include <QList>
#include <QHash>
#include "vdirectory.h"
#include "vnotebook.h"
#include "vnavigationmode.h"

class VNote;
class VEditArea;
class QLabel;

class VDirectoryTree : public QTreeWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VDirectoryTree(VNote *vnote, QWidget *parent = 0);
    inline void setEditArea(VEditArea *p_editArea);
    bool locateDirectory(const VDirectory *p_directory);
    inline const VNotebook *currentNotebook() const;

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

signals:
    void currentDirectoryChanged(VDirectory *p_directory);
    void directoryUpdated(const VDirectory *p_directory);

public slots:
    void setNotebook(VNotebook *p_notebook);
    void newRootDirectory();
    void deleteDirectory();
    void editDirectoryInfo();

    // Clear and re-build the whole directory tree.
    // Do not load all the sub-directories at once.
    void updateDirectoryTree();

private slots:
    void handleItemExpanded(QTreeWidgetItem *p_item);
    void handleItemCollapsed(QTreeWidgetItem *p_item);
    void contextMenuRequested(QPoint pos);
    void newSubDirectory();
    void currentDirectoryItemChanged(QTreeWidgetItem *currentItem);
    void copySelectedDirectories(bool p_cut = false);
    void cutSelectedDirectories();
    void pasteDirectoriesInCurDir();
    void openDirectoryLocation() const;

    // Reload the content of current directory.
    void reloadFromDisk();

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private:
    // Build the subtree of @p_parent recursively to the depth @p_depth.
    // Item @p_parent must not be built before.
    // Will expand the item if the corresponding directory was expanded before.
    // @p_depth: valid only when greater than 0.
    void updateDirectoryTreeOne(QTreeWidgetItem *p_parent, int p_depth);

    // Build the subtree of @p_parent recursively to the depth @p_depth.
    // @p_depth: negative - infinite levels.
    // Will expand the item if the corresponding directory was expanded before.
    void buildSubTree(QTreeWidgetItem *p_parent, int p_depth);

    // Fill the content of a tree item.
    void fillTreeItem(QTreeWidgetItem &p_item, const QString &p_name,
                      VDirectory *p_directory, const QIcon &p_icon);

    void initShortcuts();

    void initActions();

    // Update @p_item's direct children only: deleted, added, renamed.
    void updateItemChildren(QTreeWidgetItem *p_item);
    // Find the corresponding item of @p_dir;
    // Return's NULL if no item is found and it is the root directory if @p_widget is true.
    QTreeWidgetItem *findVDirectory(const VDirectory *p_dir, bool &p_widget);
    inline QPointer<VDirectory> getVDirectory(QTreeWidgetItem *p_item) const;
    void copyDirectoryInfoToClipboard(const QJsonArray &p_dirs, bool p_cut);
    void pasteDirectories(VDirectory *p_destDir);
    bool copyDirectory(VDirectory *p_destDir, const QString &p_destName,
                       VDirectory *p_srcDir, bool p_cut);

    // Build the subtree of @p_item's children if it has not been built yet.
    void updateChildren(QTreeWidgetItem *p_item);

    // Expand/create the directory tree nodes to @p_directory.
    QTreeWidgetItem *expandToVDirectory(const VDirectory *p_directory);

    // Expand the currently-built subtree of @p_item according to VDirectory.isExpanded().
    void expandItemTree(QTreeWidgetItem *p_item);

    QList<QTreeWidgetItem *> getVisibleItems() const;
    QList<QTreeWidgetItem *> getVisibleChildItems(const QTreeWidgetItem *p_item) const;
    bool restoreCurrentItem();

    VNote *vnote;
    QPointer<VNotebook> m_notebook;
    QVector<QPointer<VDirectory> > m_copiedDirs;
    VEditArea *m_editArea;

    // Each notebook's current item's VDirectory.
    QHash<VNotebook *, VDirectory *> m_notebookCurrentDirMap;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
    QAction *deleteDirAct;
    QAction *dirInfoAct;
    QAction *copyAct;
    QAction *cutAct;
    QAction *pasteAct;
    QAction *m_openLocationAct;

    // Reload content from disk.
    QAction *m_reloadAct;

    // Navigation Mode.
    // Map second key to QTreeWidgetItem.
    QMap<QChar, QTreeWidgetItem *> m_keyMap;
    QVector<QLabel *> m_naviLabels;

    static const QString c_infoShortcutSequence;
    static const QString c_copyShortcutSequence;
    static const QString c_cutShortcutSequence;
    static const QString c_pasteShortcutSequence;
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
