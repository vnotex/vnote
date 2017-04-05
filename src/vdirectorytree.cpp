#include <QtWidgets>
#include "vdirectorytree.h"
#include "dialog/vnewdirdialog.h"
#include "vconfigmanager.h"
#include "dialog/vdirinfodialog.h"
#include "vnote.h"
#include "vdirectory.h"
#include "utils/vutils.h"
#include "veditarea.h"

extern VNote *g_vnote;

VDirectoryTree::VDirectoryTree(VNote *vnote, QWidget *parent)
    : QTreeWidget(parent), vnote(vnote), m_editArea(NULL)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
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

void VDirectoryTree::initActions()
{
    newRootDirAct = new QAction(QIcon(":/resources/icons/create_rootdir.svg"),
                                tr("New &Root Directory"), this);
    newRootDirAct->setStatusTip(tr("Create a new root directory in current notebook"));
    connect(newRootDirAct, &QAction::triggered,
            this, &VDirectoryTree::newRootDirectory);

    newSubDirAct = new QAction(tr("&New Sub-Directory"), this);
    newSubDirAct->setStatusTip(tr("Create a new sub-directory"));
    connect(newSubDirAct, &QAction::triggered,
            this, &VDirectoryTree::newSubDirectory);

    deleteDirAct = new QAction(QIcon(":/resources/icons/delete_dir.svg"),
                               tr("&Delete"), this);
    deleteDirAct->setStatusTip(tr("Delete selected directory"));
    connect(deleteDirAct, &QAction::triggered,
            this, &VDirectoryTree::deleteDirectory);

    dirInfoAct = new QAction(QIcon(":/resources/icons/dir_info.svg"),
                             tr("&Info"), this);
    dirInfoAct->setStatusTip(tr("View and edit current directory's information"));
    connect(dirInfoAct, &QAction::triggered,
            this, &VDirectoryTree::editDirectoryInfo);

    copyAct = new QAction(QIcon(":/resources/icons/copy.svg"),
                          tr("&Copy"), this);
    copyAct->setStatusTip(tr("Copy selected directories"));
    connect(copyAct, &QAction::triggered,
            this, &VDirectoryTree::copySelectedDirectories);

    cutAct = new QAction(QIcon(":/resources/icons/cut.svg"),
                          tr("C&ut"), this);
    cutAct->setStatusTip(tr("Cut selected directories"));
    connect(cutAct, &QAction::triggered,
            this, &VDirectoryTree::cutSelectedDirectories);

    pasteAct = new QAction(QIcon(":/resources/icons/paste.svg"),
                          tr("&Paste"), this);
    pasteAct->setStatusTip(tr("Paste directories under this directory"));
    connect(pasteAct, &QAction::triggered,
            this, &VDirectoryTree::pasteDirectoriesInCurDir);
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
                            tr("Fail to open notebook %1.").arg(m_notebook->getName()), "",
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

        updateDirectoryTreeOne(item, 1);
    }
    setCurrentItem(topLevelItem(0));
}

void VDirectoryTree::updateDirectoryTreeOne(QTreeWidgetItem *p_parent, int depth)
{
    Q_ASSERT(p_parent->childCount() == 0);
    if (depth <= 0) {
        return;
    }
    VDirectory *dir = getVDirectory(p_parent);
    if (!dir->open()) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to open directory %1.").arg(dir->getName()), "",
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return;
    }
    const QVector<VDirectory *> &subDirs = dir->getSubDirs();
    for (int i = 0; i < subDirs.size(); ++i) {
        VDirectory *subDir = subDirs[i];
        QTreeWidgetItem *item = new QTreeWidgetItem(p_parent);

        fillTreeItem(*item, subDir->getName(), subDir,
                     QIcon(":/resources/icons/dir_item.svg"));

        updateDirectoryTreeOne(item, depth - 1);
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

// Update @p_item's children items
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
        updateDirectoryTreeOne(childItem, 1);
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
            updateDirectoryTreeOne(item, 1);
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

    if (item) {
        menu.addSeparator();
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

    QString info = tr("Create sub-directory under %1.").arg(curDir->getName());
    QString text(tr("Directory &name:"));
    QString defaultText("new_directory");

    do {
        VNewDirDialog dialog(tr("Create Directory"), info, text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (curDir->findSubDirectory(name)) {
                info = tr("Name already exists under %1. Please choose another name.").arg(curDir->getName());
                defaultText = name;
                continue;
            }
            VDirectory *subDir = curDir->createSubDirectory(name);
            if (!subDir) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    tr("Fail to create directory %1.").arg(name), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            updateItemChildren(curItem);
            locateDirectory(subDir);
        }
        break;
    } while (true);
}

void VDirectoryTree::newRootDirectory()
{
    if (!m_notebook) {
        return;
    }
    QString info = tr("Create root directory in notebook %1.").arg(m_notebook->getName());
    QString text(tr("Directory &name:"));
    QString defaultText("new_directory");
    VDirectory *rootDir = m_notebook->getRootDir();
    do {
        VNewDirDialog dialog(tr("Create Root Directory"), info, text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (rootDir->findSubDirectory(name)) {
                info = tr("Name already exists in notebook %1. Please choose another name.").arg(m_notebook->getName());
                defaultText = name;
                continue;
            }
            VDirectory *subDir = rootDir->createSubDirectory(name);
            if (!subDir) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    tr("Fail to create directory %1.").arg(name), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            updateItemChildren(NULL);
            locateDirectory(subDir);
        }
        break;
    } while (true);
}

void VDirectoryTree::deleteDirectory()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    VDirectory *curDir = getVDirectory(curItem);
    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                  tr("Are you sure to delete directory %1?").arg(curDir->getName()),
                                  tr("This will delete any files under this directory."), QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Ok, this);
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
    emit currentDirectoryChanged(getVDirectory(currentItem));
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
    QString info;
    QString defaultName = curName;

    do {
        VDirInfoDialog dialog(tr("Directory Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (parentDir->findSubDirectory(name)) {
                info = "Name already exists. Please choose another name.";
                defaultName = name;
                continue;
            }
            if (!curDir->rename(name)) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    tr("Fail to rename directory %1.").arg(curName), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            curItem->setText(0, name);
            curItem->setToolTip(0, name);
            emit directoryUpdated(curDir);
        }
        break;
    } while (true);
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
        dirJson["path"] = dir->retrivePath();
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
        if (!srcDir) {
            continue;
        }
        QString dirName = srcDir->getName();
        VDirectory *srcParentDir = srcDir->getParentDirectory();
        if (srcParentDir == p_destDir && !isCut) {
            // Copy and paste in the same directory.
            // Rename it to xx_copy
            dirName = VUtils::generateCopiedDirName(srcParentDir->retrivePath(), dirName);
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
    QString srcPath = QDir::cleanPath(p_srcDir->retrivePath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->retrivePath()).filePath(p_destName));
    if (srcPath == destPath) {
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
                            tr("Fail to copy directory %1.").arg(srcName),
                            tr("Please check if there already exists a directory with the same name."),
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
        qDebug() << "locate directory" << p_directory->retrivePath()
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
            updateDirectoryTreeOne(pItem, 1);
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
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            setCurrentItem(it.value());
            setFocus();
            p_succeed = true;
            ret = true;
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key.
        secondKey = true;
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
