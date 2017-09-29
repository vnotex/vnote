#include <QtWidgets>
#include "vdirectorytree.h"
#include "dialog/vnewdirdialog.h"
#include "vconfigmanager.h"
#include "dialog/vdirinfodialog.h"
#include "vnote.h"
#include "vdirectory.h"
#include "utils/vutils.h"
#include "veditarea.h"
#include "vconfigmanager.h"
#include "vmainwindow.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;
extern VNote *g_vnote;

const QString VDirectoryTree::c_infoShortcutSequence = "F2";
const QString VDirectoryTree::c_copyShortcutSequence = "Ctrl+C";
const QString VDirectoryTree::c_cutShortcutSequence = "Ctrl+X";
const QString VDirectoryTree::c_pasteShortcutSequence = "Ctrl+V";

VDirectoryTree::VDirectoryTree(VNote *vnote, QWidget *parent)
    : QTreeWidget(parent), VNavigationMode(),
      vnote(vnote), m_editArea(NULL)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    initShortcuts();
    initActions();

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(handleItemExpanded(QTreeWidgetItem*)));
    connect(this, SIGNAL(itemCollapsed(QTreeWidgetItem*)),
            this, SLOT(handleItemCollapsed(QTreeWidgetItem*)));
    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenuRequested(QPoint)));
    connect(this, &VDirectoryTree::currentItemChanged,
            this, &VDirectoryTree::currentDirectoryItemChanged);
}

void VDirectoryTree::initShortcuts()
{
    QShortcut *infoShortcut = new QShortcut(QKeySequence(c_infoShortcutSequence), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, [this](){
                editDirectoryInfo();
            });

    QShortcut *copyShortcut = new QShortcut(QKeySequence(c_copyShortcutSequence), this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated,
            this, [this](){
                copySelectedDirectories();
            });

    QShortcut *cutShortcut = new QShortcut(QKeySequence(c_cutShortcutSequence), this);
    cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cutShortcut, &QShortcut::activated,
            this, [this](){
                cutSelectedDirectories();
            });

    QShortcut *pasteShortcut = new QShortcut(QKeySequence(c_pasteShortcutSequence), this);
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated,
            this, [this](){
                pasteDirectoriesInCurDir();
            });
}

void VDirectoryTree::initActions()
{
    newRootDirAct = new QAction(QIcon(":/resources/icons/create_rootdir.svg"),
                                tr("New &Root Folder"), this);
    newRootDirAct->setToolTip(tr("Create a root folder in current notebook"));
    connect(newRootDirAct, &QAction::triggered,
            this, &VDirectoryTree::newRootDirectory);

    newSubDirAct = new QAction(tr("&New Subfolder"), this);
    newSubDirAct->setToolTip(tr("Create a subfolder"));
    connect(newSubDirAct, &QAction::triggered,
            this, &VDirectoryTree::newSubDirectory);

    deleteDirAct = new QAction(QIcon(":/resources/icons/delete_dir.svg"),
                               tr("&Delete"), this);
    deleteDirAct->setToolTip(tr("Delete selected folder"));
    connect(deleteDirAct, &QAction::triggered,
            this, &VDirectoryTree::deleteDirectory);

    dirInfoAct = new QAction(QIcon(":/resources/icons/dir_info.svg"),
                             tr("&Info\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)), this);
    dirInfoAct->setToolTip(tr("View and edit current folder's information"));
    connect(dirInfoAct, &QAction::triggered,
            this, &VDirectoryTree::editDirectoryInfo);

    copyAct = new QAction(QIcon(":/resources/icons/copy.svg"),
                          tr("&Copy\t%1").arg(VUtils::getShortcutText(c_copyShortcutSequence)), this);
    copyAct->setToolTip(tr("Copy selected folders"));
    connect(copyAct, &QAction::triggered,
            this, &VDirectoryTree::copySelectedDirectories);

    cutAct = new QAction(QIcon(":/resources/icons/cut.svg"),
                         tr("C&ut\t%1").arg(VUtils::getShortcutText(c_cutShortcutSequence)), this);
    cutAct->setToolTip(tr("Cut selected folders"));
    connect(cutAct, &QAction::triggered,
            this, &VDirectoryTree::cutSelectedDirectories);

    pasteAct = new QAction(QIcon(":/resources/icons/paste.svg"),
                           tr("&Paste\t%1").arg(VUtils::getShortcutText(c_pasteShortcutSequence)), this);
    pasteAct->setToolTip(tr("Paste folders in this folder"));
    connect(pasteAct, &QAction::triggered,
            this, &VDirectoryTree::pasteDirectoriesInCurDir);

    m_openLocationAct = new QAction(tr("&Open Folder Location"), this);
    m_openLocationAct->setToolTip(tr("Open the folder containing this folder in operating system"));
    connect(m_openLocationAct, &QAction::triggered,
            this, &VDirectoryTree::openDirectoryLocation);

    m_reloadAct = new QAction(tr("&Reload From Disk"), this);
    m_reloadAct->setToolTip(tr("Reload the content of this folder (or notebook) from disk"));
    connect(m_reloadAct, &QAction::triggered,
            this, &VDirectoryTree::reloadFromDisk);
}

void VDirectoryTree::setNotebook(VNotebook *p_notebook)
{
    setFocus();
    if (m_notebook == p_notebook) {
        return;
    }

    if (m_notebook) {
        // Disconnect
        disconnect((VNotebook *)m_notebook, &VNotebook::contentChanged,
                   this, &VDirectoryTree::updateDirectoryTree);
    }
    m_notebook = p_notebook;
    if (m_notebook) {
        connect((VNotebook *)m_notebook, &VNotebook::contentChanged,
                this, &VDirectoryTree::updateDirectoryTree);
    } else {
        clear();
        return;
    }
    if (!m_notebook->open()) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to open notebook <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle).arg(m_notebook->getName()),
                            tr("Please check if path <span style=\"%1\">%2</span> exists.")
                              .arg(g_config->c_dataTextStyle).arg(m_notebook->getPath()),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        clear();
        return;
    }
    updateDirectoryTree();
}

void VDirectoryTree::fillTreeItem(QTreeWidgetItem &p_item, const QString &p_name,
                                  VDirectory *p_directory, const QIcon &p_icon)
{
    p_item.setText(0, p_name);
    p_item.setToolTip(0, p_name);
    p_item.setData(0, Qt::UserRole, QVariant::fromValue(p_directory));
    p_item.setIcon(0, p_icon);
}

void VDirectoryTree::updateDirectoryTree()
{
    clear();
    VDirectory *rootDir = m_notebook->getRootDir();
    const QVector<VDirectory *> &subDirs = rootDir->getSubDirs();
    for (int i = 0; i < subDirs.size(); ++i) {
        VDirectory *dir = subDirs[i];
        QTreeWidgetItem *item = new QTreeWidgetItem(this);

        fillTreeItem(*item, dir->getName(), dir,
                     QIcon(":/resources/icons/dir_item.svg"));

        buildSubTree(item, 1);
    }

    if (!restoreCurrentItem()) {
        setCurrentItem(topLevelItem(0));
    }
}

bool VDirectoryTree::restoreCurrentItem()
{
    auto it = m_notebookCurrentDirMap.find(m_notebook);
    if (it != m_notebookCurrentDirMap.end()) {
        bool rootDirectory;
        QTreeWidgetItem *item = findVDirectory(it.value(), rootDirectory);
        if (item) {
            setCurrentItem(item);
            return true;
        }
    }

    return false;
}

void VDirectoryTree::buildSubTree(QTreeWidgetItem *p_parent, int p_depth)
{
    if (p_depth == 0) {
        return;
    }

    VDirectory *dir = getVDirectory(p_parent);
    if (!dir->open()) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to open folder <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle).arg(dir->getName()),
                            tr("Please check if path <span style=\"%1\">%2</span> exists.")
                              .arg(g_config->c_dataTextStyle).arg(dir->fetchPath()),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return;
    }

    if (p_parent->childCount() > 0) {
        // This directory has been built before. Try its children directly.
        for (int i = 0; i < p_parent->childCount(); ++i) {
            buildSubTree(p_parent->child(i), p_depth -1);
        }
    } else {
        const QVector<VDirectory *> &subDirs = dir->getSubDirs();
        for (int i = 0; i < subDirs.size(); ++i) {
            VDirectory *subDir = subDirs[i];
            QTreeWidgetItem *item = new QTreeWidgetItem(p_parent);

            fillTreeItem(*item, subDir->getName(), subDir,
                         QIcon(":/resources/icons/dir_item.svg"));

            buildSubTree(item, p_depth - 1);
        }
    }

    if (dir->isExpanded()) {
        expandItem(p_parent);
    }
}

void VDirectoryTree::handleItemCollapsed(QTreeWidgetItem *p_item)
{
    VDirectory *dir = getVDirectory(p_item);
    dir->setExpanded(false);
}

void VDirectoryTree::handleItemExpanded(QTreeWidgetItem *p_item)
{
    updateChildren(p_item);
    VDirectory *dir = getVDirectory(p_item);
    dir->setExpanded(true);
}

void VDirectoryTree::updateChildren(QTreeWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    int nrChild = p_item->childCount();
    if (nrChild == 0) {
        return;
    }

    for (int i = 0; i < nrChild; ++i) {
        QTreeWidgetItem *childItem = p_item->child(i);
        if (childItem->childCount() > 0) {
            continue;
        }

        buildSubTree(childItem, 1);
    }
}

void VDirectoryTree::updateItemChildren(QTreeWidgetItem *p_item)
{
    QPointer<VDirectory> parentDir;
    if (p_item) {
        parentDir = getVDirectory(p_item);
    } else {
        parentDir = m_notebook->getRootDir();
    }
    const QVector<VDirectory *> &dirs = parentDir->getSubDirs();

    QHash<VDirectory *, QTreeWidgetItem *> itemDirMap;
    int nrChild = p_item ? p_item->childCount() : topLevelItemCount();
    for (int i = 0; i < nrChild; ++i) {
        QTreeWidgetItem *item = p_item ? p_item->child(i) : topLevelItem(i);
        itemDirMap.insert(getVDirectory(item), item);
    }

    for (int i = 0; i < dirs.size(); ++i) {
        VDirectory *dir = dirs[i];
        QTreeWidgetItem *item = itemDirMap.value(dir, NULL);
        if (item) {
            if (p_item) {
                p_item->removeChild(item);
                p_item->insertChild(i, item);
            } else {
                int topIdx = indexOfTopLevelItem(item);
                takeTopLevelItem(topIdx);
                insertTopLevelItem(i, item);
            }
            itemDirMap.remove(dir);
        } else {
            // Insert a new item
            if (p_item) {
                item = new QTreeWidgetItem(p_item);
            } else {
                item = new QTreeWidgetItem(this);
            }
            fillTreeItem(*item, dir->getName(), dir, QIcon(":/resources/icons/dir_item.svg"));
            buildSubTree(item, 1);
        }
        expandItemTree(item);
    }

    // Delete items without corresponding VDirectory
    for (auto iter = itemDirMap.begin(); iter != itemDirMap.end(); ++iter) {
        QTreeWidgetItem *item = iter.value();
        if (p_item) {
            p_item->removeChild(item);
        } else {
            int topIdx = indexOfTopLevelItem(item);
            takeTopLevelItem(topIdx);
        }
        delete item;
    }
}

void VDirectoryTree::contextMenuRequested(QPoint pos)
{
    QTreeWidgetItem *item = itemAt(pos);

    if (!m_notebook) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (!item) {
        // Context menu on the free space of the QTreeWidget
        menu.addAction(newRootDirAct);
    } else {
        // Context menu on a QTreeWidgetItem
        if (item->parent()) {
            // Low-level item
            menu.addAction(newSubDirAct);
        } else {
            // Top-level item
            menu.addAction(newRootDirAct);
            menu.addAction(newSubDirAct);
        }
        menu.addAction(deleteDirAct);
        menu.addSeparator();
        menu.addAction(copyAct);
        menu.addAction(cutAct);
    }

    if (VUtils::opTypeInClipboard() == ClipboardOpType::CopyDir
        && !m_copiedDirs.isEmpty()) {
        if (!item) {
            menu.addSeparator();
        }
        menu.addAction(pasteAct);
    }

    menu.addSeparator();
    menu.addAction(m_reloadAct);

    if (item) {
        menu.addAction(m_openLocationAct);
        menu.addAction(dirInfoAct);
    }

    menu.exec(mapToGlobal(pos));
}

void VDirectoryTree::newSubDirectory()
{
    if (!m_notebook) {
        return;
    }
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    VDirectory *curDir = getVDirectory(curItem);

    QString info = tr("Create a subfolder in <span style=\"%1\">%2</span>.")
                     .arg(g_config->c_dataTextStyle).arg(curDir->getName());
    QString defaultName("new_folder");
    defaultName = VUtils::getFileNameWithSequence(curDir->fetchPath(), defaultName);
    VNewDirDialog dialog(tr("Create Folder"), info, defaultName, curDir, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getNameInput();

        VDirectory *subDir = curDir->createSubDirectory(name);
        if (!subDir) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to create folder <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(name), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        updateItemChildren(curItem);
        locateDirectory(subDir);
    }
}

void VDirectoryTree::newRootDirectory()
{
    if (!m_notebook) {
        return;
    }
    QString info = tr("Create a root folder in notebook <span style=\"%1\">%2</span>.")
                     .arg(g_config->c_dataTextStyle).arg(m_notebook->getName());
    VDirectory *rootDir = m_notebook->getRootDir();
    QString defaultName("new_folder");
    defaultName = VUtils::getFileNameWithSequence(rootDir->fetchPath(), defaultName);
    VNewDirDialog dialog(tr("Create Root Folder"), info, defaultName, rootDir, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getNameInput();

        VDirectory *subDir = rootDir->createSubDirectory(name);
        if (!subDir) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to create folder <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(name), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        updateItemChildren(NULL);
        locateDirectory(subDir);
    }
}

void VDirectoryTree::deleteDirectory()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    VDirectory *curDir = getVDirectory(curItem);
    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                  tr("Are you sure to delete folder <span style=\"%1\">%2</span>?")
                                    .arg(g_config->c_dataTextStyle).arg(curDir->getName()),
                                  tr("<span style=\"%1\">WARNING</span>: "
                                     "VNote will delete the whole directory "
                                     "<span style=\"%2\">%3</span>."
                                     "You could find deleted files in the recycle bin "
                                     "of this notebook.<br>The operation is IRREVERSIBLE!")
                                    .arg(g_config->c_warningTextStyle).arg(g_config->c_dataTextStyle).arg(curDir->fetchPath()),
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Ok, this, MessageBoxType::Danger);
    if (ret == QMessageBox::Ok) {
        m_editArea->closeFile(curDir, true);
        VDirectory *parentDir = curDir->getParentDirectory();
        Q_ASSERT(parentDir);
        parentDir->deleteSubDirectory(curDir);
        delete curItem;
    }
}

void VDirectoryTree::currentDirectoryItemChanged(QTreeWidgetItem *currentItem)
{
    if (!currentItem) {
        emit currentDirectoryChanged(NULL);
        return;
    }

    QPointer<VDirectory> dir = getVDirectory(currentItem);
    m_notebookCurrentDirMap[m_notebook] = dir;
    emit currentDirectoryChanged(dir);
}

void VDirectoryTree::editDirectoryInfo()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }

    VDirectory *curDir = getVDirectory(curItem);
    VDirectory *parentDir = curDir->getParentDirectory();
    QString curName = curDir->getName();

    VDirInfoDialog dialog(tr("Folder Information"), "", curDir, parentDir, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getNameInput();
        if (name == curName) {
            return;
        }

        if (!curDir->rename(name)) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to rename folder <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(curName), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        curItem->setText(0, name);
        curItem->setToolTip(0, name);
        emit directoryUpdated(curDir);
    }
}

void VDirectoryTree::openDirectoryLocation() const
{
    QTreeWidgetItem *curItem = currentItem();
    V_ASSERT(curItem);
    QUrl url = QUrl::fromLocalFile(getVDirectory(curItem)->fetchBasePath());
    QDesktopServices::openUrl(url);
}

void VDirectoryTree::reloadFromDisk()
{
    if (!m_notebook) {
        return;
    }

    QString msg;
    QString info;
    VDirectory *curDir = NULL;
    QTreeWidgetItem *curItem = currentItem();
    if (curItem) {
        // Reload current directory.
        curDir = getVDirectory(curItem);
        info = tr("Are you sure to reload folder <span style=\"%1\">%2</span>?")
                 .arg(g_config->c_dataTextStyle).arg(curDir->getName());
        msg = tr("Folder %1 reloaded from disk").arg(curDir->getName());
    } else {
        // Reload notebook.
        info = tr("Are you sure to reload notebook <span style=\"%1\">%2</span>?")
                 .arg(g_config->c_dataTextStyle).arg(m_notebook->getName());
        msg = tr("Notebook %1 reloaded from disk").arg(m_notebook->getName());
    }

    if (g_config->getConfirmReloadFolder()) {
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      info,
                                      tr("VNote will close all the related notes before reload."),
                                      QMessageBox::Ok | QMessageBox::YesToAll | QMessageBox::Cancel,
                                      QMessageBox::Ok,
                                      this);
        switch (ret) {
        case QMessageBox::YesToAll:
            g_config->setConfirmReloadFolder(false);
            // Fall through.

        case QMessageBox::Ok:
            break;

        case QMessageBox::Cancel:
            return;

        default:
            return;
        }
    }

    m_notebookCurrentDirMap.remove(m_notebook);

    if (curItem) {
        if (!m_editArea->closeFile(curDir, false)) {
            return;
        }

        setCurrentItem(NULL);

        curItem->setExpanded(false);
        curDir->setExpanded(false);

        curDir->close();

        // Remove all its children.
        QList<QTreeWidgetItem *> children = curItem->takeChildren();
        for (int i = 0; i < children.size(); ++i) {
            delete children[i];
        }

        buildSubTree(curItem, 1);

        setCurrentItem(curItem);
    } else {
        if (!m_editArea->closeFile(m_notebook, false)) {
            return;
        }

        m_notebook->close();

        if (!m_notebook->open()) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to open notebook <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(m_notebook->getName()),
                                tr("Please check if path <span style=\"%1\">%2</span> exists.")
                                  .arg(g_config->c_dataTextStyle).arg(m_notebook->getPath()),
                                QMessageBox::Ok, QMessageBox::Ok, this);
            clear();
            return;
        }

        updateDirectoryTree();
    }

    if (!msg.isEmpty()) {
        g_mainWin->showStatusMessage(msg);
    }
}

void VDirectoryTree::copySelectedDirectories(bool p_cut)
{
    QList<QTreeWidgetItem *> items = selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QJsonArray dirs;
    m_copiedDirs.clear();
    for (int i = 0; i < items.size(); ++i) {
        VDirectory *dir = getVDirectory(items[i]);
        QJsonObject dirJson;
        dirJson["notebook"] = dir->getNotebookName();
        dirJson["path"] = dir->fetchPath();
        dirs.append(dirJson);

        m_copiedDirs.append(dir);
    }

    copyDirectoryInfoToClipboard(dirs, p_cut);
}

void VDirectoryTree::copyDirectoryInfoToClipboard(const QJsonArray &p_dirs, bool p_cut)
{
    QJsonObject clip;
    clip["operation"] = (int)ClipboardOpType::CopyDir;
    clip["is_cut"] = p_cut;
    clip["sources"] = p_dirs;

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(QJsonDocument(clip).toJson(QJsonDocument::Compact));
}

void VDirectoryTree::cutSelectedDirectories()
{
    copySelectedDirectories(true);
}

void VDirectoryTree::pasteDirectoriesInCurDir()
{
    if (m_copiedDirs.isEmpty()) {
        return;
    }

    QTreeWidgetItem *item = currentItem();
    VDirectory *destDir = m_notebook->getRootDir();
    if (item) {
        destDir = getVDirectory(item);
    }

    pasteDirectories(destDir);
}

void VDirectoryTree::pasteDirectories(VDirectory *p_destDir)
{
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    QJsonObject clip = QJsonDocument::fromJson(text.toLocal8Bit()).object();
    Q_ASSERT(!clip.isEmpty() && clip["operation"] == (int)ClipboardOpType::CopyDir);
    bool isCut = clip["is_cut"].toBool();

    int nrPasted = 0;
    for (int i = 0; i < m_copiedDirs.size(); ++i) {
        QPointer<VDirectory> srcDir = m_copiedDirs[i];
        if (!srcDir || srcDir == p_destDir) {
            continue;
        }

        QString dirName = srcDir->getName();
        VDirectory *srcParentDir = srcDir->getParentDirectory();
        if (srcParentDir == p_destDir && !isCut) {
            // Copy and paste in the same directory.
            // Rename it to xx_copy
            dirName = VUtils::generateCopiedDirName(srcParentDir->fetchPath(), dirName);
        }
        if (copyDirectory(p_destDir, dirName, srcDir, isCut)) {
            nrPasted++;
        }
    }
    qDebug() << "pasted" << nrPasted << "files successfully";
    clipboard->clear();
    m_copiedDirs.clear();
}

void VDirectoryTree::mousePressEvent(QMouseEvent *event)
{
    QTreeWidgetItem *item = itemAt(event->pos());
    if (!item) {
        setCurrentItem(NULL);
    }
    QTreeWidget::mousePressEvent(event);
}

void VDirectoryTree::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();
    int modifiers = event->modifiers();

    switch (key) {
    case Qt::Key_Return:
    {
        QTreeWidgetItem *item = currentItem();
        if (item) {
            item->setExpanded(!item->isExpanded());
        }

        break;
    }

    case Qt::Key_J:
    {
        if (modifiers == Qt::ControlModifier) {
            event->accept();
            QKeyEvent *downEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down,
                                                 Qt::NoModifier);
            QCoreApplication::postEvent(this, downEvent);
            return;
        }

        break;
    }

    case Qt::Key_K:
    {
        if (modifiers == Qt::ControlModifier) {
            event->accept();
            QKeyEvent *upEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up,
                                               Qt::NoModifier);
            QCoreApplication::postEvent(this, upEvent);
            return;
        }

        break;
    }

    case Qt::Key_Asterisk:
    {
        if (modifiers == Qt::ShiftModifier) {
            // *, by default will expand current item recursively.
            // We build the tree recursively before the expanding.
            QTreeWidgetItem *item = currentItem();
            if (item) {
                buildSubTree(item, -1);
            }
        }

        break;
    }

    default:
        break;
    }

    QTreeWidget::keyPressEvent(event);
}

bool VDirectoryTree::copyDirectory(VDirectory *p_destDir, const QString &p_destName,
                                   VDirectory *p_srcDir, bool p_cut)
{
    qDebug() << "copy" << p_srcDir->getName() << "to" << p_destDir->getName()
             << "as" << p_destName;
    QString srcName = p_srcDir->getName();
    QString srcPath = QDir::cleanPath(p_srcDir->fetchPath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->fetchPath()).filePath(p_destName));
    if (VUtils::equalPath(srcPath, destPath)) {
        return true;
    }

    VDirectory *srcParentDir = p_srcDir->getParentDirectory();
    VDirectory *destDir = VDirectory::copyDirectory(p_destDir, p_destName, p_srcDir, p_cut);

    if (destDir) {
        // Update QTreeWidget
        bool isWidget;
        QTreeWidgetItem *destItem = findVDirectory(p_destDir, isWidget);
        if (destItem || isWidget) {
            updateItemChildren(destItem);
        }

        if (p_cut) {
            QTreeWidgetItem *srcItem = findVDirectory(srcParentDir, isWidget);
            if (srcItem || isWidget) {
                updateItemChildren(srcItem);
            }
        }

        // Broadcast this update
        emit directoryUpdated(destDir);
    } else {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to copy folder <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle).arg(srcName),
                            tr("Please check if there already exists a folder with the same name."),
                            QMessageBox::Ok, QMessageBox::Ok, this);
    }

    return destDir;
}

QTreeWidgetItem *VDirectoryTree::findVDirectory(const VDirectory *p_dir, bool &p_widget)
{
    p_widget = false;
    if (!p_dir) {
        return NULL;
    } else if (p_dir->getNotebookName() != m_notebook->getName()) {
        return NULL;
    } else if (p_dir == m_notebook->getRootDir()) {
        p_widget = true;
        return NULL;
    }

    bool isWidget;
    QTreeWidgetItem *pItem = findVDirectory(p_dir->getParentDirectory(), isWidget);
    if (pItem) {
        // Iterate all its children to find the match.
        int nrChild = pItem->childCount();
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *item = pItem->child(i);
            if (getVDirectory(item) == p_dir) {
                return item;
            }
        }
    } else if (isWidget) {
        // Iterate all the top-level items.
        int nrChild = topLevelItemCount();
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *item = topLevelItem(i);
            if (getVDirectory(item) == p_dir) {
                return item;
            }
        }
    }
    return NULL;
}

bool VDirectoryTree::locateDirectory(const VDirectory *p_directory)
{
    if (p_directory) {
        qDebug() << "locate folder" << p_directory->fetchPath()
                 << "in" << m_notebook->getName();
        if (p_directory->getNotebook() != m_notebook) {
            return false;
        }
        QTreeWidgetItem *item = expandToVDirectory(p_directory);
        if (item) {
            setCurrentItem(item);
        }
        return item;
    }
    return false;
}

QTreeWidgetItem *VDirectoryTree::expandToVDirectory(const VDirectory *p_directory)
{
    if (!p_directory || p_directory->getNotebook() != m_notebook
        || p_directory == m_notebook->getRootDir()) {
        return NULL;
    }

    if (p_directory->getParentDirectory() == m_notebook->getRootDir()) {
        // Top-level items.
        int nrChild = topLevelItemCount();
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *item = topLevelItem(i);
            if (getVDirectory(item) == p_directory) {
                return item;
            }
        }
    } else {
        QTreeWidgetItem *pItem = expandToVDirectory(p_directory->getParentDirectory());
        if (!pItem) {
            return NULL;
        }
        int nrChild = pItem->childCount();
        if (nrChild == 0) {
            buildSubTree(pItem, 1);
        }
        nrChild = pItem->childCount();
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *item = pItem->child(i);
            if (getVDirectory(item) == p_directory) {
                return item;
            }
        }
    }
    return NULL;
}

void VDirectoryTree::expandItemTree(QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    VDirectory *dir = getVDirectory(p_item);
    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        expandItemTree(p_item->child(i));
    }

    if (dir->isExpanded()) {
        Q_ASSERT(nrChild > 0);
        expandItem(p_item);
    }
}

void VDirectoryTree::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
    V_ASSERT(m_keyMap.empty());
    V_ASSERT(m_naviLabels.empty());
}

void VDirectoryTree::showNavigation()
{
    // Clean up.
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }
    m_naviLabels.clear();

    if (!isVisible()) {
        return;
    }

    // Generate labels for visible items.
    auto items = getVisibleItems();
    for (int i = 0; i < 26 && i < items.size(); ++i) {
        QChar key('a' + i);
        m_keyMap[key] = items[i];

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, this);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->move(visualItemRect(items[i]).topLeft());
        label->show();
        m_naviLabels.append(label);
    }
}

void VDirectoryTree::hideNavigation()
{
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }
    m_naviLabels.clear();
}

bool VDirectoryTree::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (secondKey && !keyChar.isNull()) {
        secondKey = false;
        p_succeed = true;
        ret = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            setCurrentItem(it.value());
            setFocus();
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            secondKey = true;
        }
        ret = true;
    }
    return ret;
}

QList<QTreeWidgetItem *> VDirectoryTree::getVisibleItems() const
{
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        if (!item->isHidden()) {
            items.append(item);
            if (item->isExpanded()) {
                items.append(getVisibleChildItems(item));
            }
        }
    }
    return items;
}

QList<QTreeWidgetItem *> VDirectoryTree::getVisibleChildItems(const QTreeWidgetItem *p_item) const
{
    QList<QTreeWidgetItem *> items;
    if (p_item && !p_item->isHidden() && p_item->isExpanded()) {
        for (int i = 0; i < p_item->childCount(); ++i) {
            QTreeWidgetItem *child = p_item->child(i);
            if (!child->isHidden()) {
                items.append(child);
                if (child->isExpanded()) {
                    items.append(getVisibleChildItems(child));
                }
            }
        }
    }
    return items;
}
