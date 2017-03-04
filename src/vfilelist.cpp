#include <QtDebug>
#include <QtWidgets>
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"
#include "dialog/vfileinfodialog.h"
#include "vnote.h"
#include "veditarea.h"
#include "utils/vutils.h"
#include "vfile.h"

VFileList::VFileList(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initActions();
}

void VFileList::setupUI()
{
    fileList = new QListWidget(this);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList->setDragDropMode(QAbstractItemView::InternalMove);
    fileList->setObjectName("FileList");

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(fileList);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    connect(fileList, &QListWidget::customContextMenuRequested,
            this, &VFileList::contextMenuRequested);
    connect(fileList, &QListWidget::itemClicked,
            this, &VFileList::handleItemClicked);
    connect(fileList->model(), &QAbstractItemModel::rowsMoved,
            this, &VFileList::handleRowsMoved);

    setLayout(mainLayout);
}

void VFileList::initActions()
{
    newFileAct = new QAction(QIcon(":/resources/icons/create_note.svg"),
                             tr("&New Note"), this);
    newFileAct->setStatusTip(tr("Create a note in current directory"));
    connect(newFileAct, SIGNAL(triggered(bool)),
            this, SLOT(newFile()));

    deleteFileAct = new QAction(QIcon(":/resources/icons/delete_note.svg"),
                                tr("&Delete"), this);
    deleteFileAct->setStatusTip(tr("Delete selected note"));
    connect(deleteFileAct, SIGNAL(triggered(bool)),
            this, SLOT(deleteFile()));

    fileInfoAct = new QAction(QIcon(":/resources/icons/note_info.svg"),
                              tr("&Info"), this);
    fileInfoAct->setStatusTip(tr("View and edit current note's information"));
    connect(fileInfoAct, SIGNAL(triggered(bool)),
            this, SLOT(fileInfo()));

    copyAct = new QAction(QIcon(":/resources/icons/copy.svg"),
                          tr("&Copy"), this);
    copyAct->setStatusTip(tr("Copy selected notes"));
    connect(copyAct, &QAction::triggered,
            this, &VFileList::copySelectedFiles);

    cutAct = new QAction(QIcon(":/resources/icons/cut.svg"),
                          tr("C&ut"), this);
    cutAct->setStatusTip(tr("Cut selected notes"));
    connect(cutAct, &QAction::triggered,
            this, &VFileList::cutSelectedFiles);

    pasteAct = new QAction(QIcon(":/resources/icons/paste.svg"),
                          tr("&Paste"), this);
    pasteAct->setStatusTip(tr("Paste notes in current directory"));
    connect(pasteAct, &QAction::triggered,
            this, &VFileList::pasteFilesInCurDir);
}

void VFileList::setDirectory(VDirectory *p_directory)
{
    if (m_directory == p_directory) {
        return;
    }
    m_directory = p_directory;
    if (!m_directory) {
        fileList->clear();
        return;
    }

    qDebug() << "filelist set directory" << m_directory->getName();
    updateFileList();
}

void VFileList::updateFileList()
{
    fileList->clear();
    if (!m_directory->open()) {
        return;
    }
    const QVector<VFile *> &files = m_directory->getFiles();
    for (int i = 0; i < files.size(); ++i) {
        VFile *file = files[i];
        insertFileListItem(file);
    }
}

void VFileList::fileInfo()
{
    QListWidgetItem *curItem = fileList->currentItem();
    Q_ASSERT(curItem);
    fileInfo(getVFile(curItem));
}

void VFileList::fileInfo(VFile *p_file)
{
    if (!p_file) {
        return;
    }
    VDirectory *dir = p_file->getDirectory();
    QString info;
    QString defaultName = p_file->getName();
    QString curName = defaultName;
    do {
        VFileInfoDialog dialog(tr("Note Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (dir->findFile(name)) {
                info = "Name already exists. Please choose another name.";
                defaultName = name;
                continue;
            }
            copyFile(dir, name, p_file, true);
        }
        break;
    } while (true);
}

QListWidgetItem* VFileList::insertFileListItem(VFile *file, bool atFront)
{
    Q_ASSERT(file);
    QString fileName = file->getName();
    QListWidgetItem *item = new QListWidgetItem(fileName);
    unsigned long long ptr = (long long)file;
    item->setData(Qt::UserRole, ptr);
    item->setToolTip(fileName);
    Q_ASSERT(sizeof(file) <= sizeof(ptr));
    if (atFront) {
        fileList->insertItem(0, item);
    } else {
        fileList->addItem(item);
    }
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    qDebug() << "VFileList adds" << fileName;
    return item;
}

void VFileList::removeFileListItem(QListWidgetItem *item)
{
    fileList->setCurrentRow(-1);
    fileList->removeItemWidget(item);
    delete item;
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
}

void VFileList::newFile()
{
    if (!m_directory) {
        return;
    }
    QString info = QString("Create a note under %1.").arg(m_directory->getName());
    QString text("&Note name:");
    QString defaultText("new_note");
    do {
        VNewFileDialog dialog(QString("Create Note"), info, text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (m_directory->findFile(name)) {
                info = "Name already exists. Please choose another name.";
                defaultText = name;
                continue;
            }
            VFile *file = m_directory->createFile(name);
            if (!file) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    QString("Fail to create note %1.").arg(name), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            QVector<QListWidgetItem *> items = updateFileListAdded();
            Q_ASSERT(items.size() == 1);
            fileList->setCurrentItem(items[0]);
            // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
            fileList->update();

            // Open it in edit mode
            emit fileCreated(file, OpenFileMode::Edit);
        }
        break;
    } while (true);
}

QVector<QListWidgetItem *> VFileList::updateFileListAdded()
{
    QVector<QListWidgetItem *> ret;
    const QVector<VFile *> &files = m_directory->getFiles();
    for (int i = 0; i < files.size(); ++i) {
        VFile *file = files[i];
        if (i >= fileList->count()) {
            QListWidgetItem *item = insertFileListItem(file, false);
            ret.append(item);
        } else {
            VFile *itemFile = getVFile(fileList->item(i));
            if (itemFile != file) {
                QListWidgetItem *item = insertFileListItem(file, false);
                ret.append(item);
            }
        }
    }
    qDebug() << ret.size() << "items added";
    return ret;
}

// Delete the file related to current item
void VFileList::deleteFile()
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    Q_ASSERT(!items.isEmpty());
    for (int i = 0; i < items.size(); ++i) {
        deleteFile(getVFile(items.at(i)));
    }
}

// @p_file may or may not be listed in VFileList
void VFileList::deleteFile(VFile *p_file)
{
    if (!p_file) {
        return;
    }
    VDirectory *dir = p_file->getDirectory();
    QString fileName = p_file->getName();
    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                       QString("Are you sure to delete note %1?").arg(fileName), tr("This may be unrecoverable!"),
                       QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok, this);
    if (ret == QMessageBox::Ok) {
        editArea->closeFile(p_file, true);

        // Remove the item before deleting it totally, or p_file will be invalid.
        QListWidgetItem *item = findItem(p_file);
        if (item) {
            removeFileListItem(item);
        }

        dir->deleteFile(p_file);
    }
}

void VFileList::contextMenuRequested(QPoint pos)
{
    QListWidgetItem *item = fileList->itemAt(pos);
    QMenu menu(this);

    if (!m_directory) {
        return;
    }
    menu.addAction(newFileAct);
    if (item) {
        menu.addAction(deleteFileAct);
        menu.addSeparator();
        menu.addAction(copyAct);
        menu.addAction(cutAct);
    }

    if (VUtils::opTypeInClipboard() == ClipboardOpType::CopyFile
        && !m_copiedFiles.isEmpty()) {
        if (!item) {
            menu.addSeparator();
        }
        menu.addAction(pasteAct);
    }

    if (item && fileList->selectedItems().size() == 1) {
        menu.addSeparator();
        menu.addAction(fileInfoAct);
    }
    menu.exec(fileList->mapToGlobal(pos));
}

QListWidgetItem* VFileList::findItem(const VFile *p_file)
{
    if (!p_file || p_file->getDirectory() != m_directory) {
        return NULL;
    }

    int nrChild = fileList->count();
    for (int i = 0; i < nrChild; ++i) {
        QListWidgetItem *item = fileList->item(i);
        if (p_file == getVFile(item)) {
            return item;
        }
    }
    return NULL;
}

void VFileList::handleItemClicked(QListWidgetItem *currentItem)
{
    if (!currentItem) {
        emit fileClicked(NULL);
        return;
    }
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    emit fileClicked(getVFile(currentItem), OpenFileMode::Read);
}

bool VFileList::importFile(const QString &p_srcFilePath)
{
    if (p_srcFilePath.isEmpty()) {
        return false;
    }
    Q_ASSERT(m_directory);
    // Copy file @name to current directory
    QString targetPath = m_directory->retrivePath();
    QString srcName = VUtils::fileNameFromPath(p_srcFilePath);
    if (srcName.isEmpty()) {
        return false;
    }
    QString targetFilePath = QDir(targetPath).filePath(srcName);
    bool ret = VUtils::copyFile(p_srcFilePath, targetFilePath, false);
    if (!ret) {
        return false;
    }

    VFile *destFile = m_directory->addFile(srcName, -1);
    if (destFile) {
        return insertFileListItem(destFile, false);
    }
    return false;
}

void VFileList::copySelectedFiles(bool p_isCut)
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QJsonArray files;
    m_copiedFiles.clear();
    for (int i = 0; i < items.size(); ++i) {
        VFile *file = getVFile(items[i]);
        QJsonObject fileJson;
        fileJson["notebook"] = file->getNotebookName();
        fileJson["path"] = file->retrivePath();
        files.append(fileJson);

        m_copiedFiles.append(file);
    }

    copyFileInfoToClipboard(files, p_isCut);
}

void VFileList::cutSelectedFiles()
{
    copySelectedFiles(true);
}

void VFileList::copyFileInfoToClipboard(const QJsonArray &p_files, bool p_isCut)
{
    QJsonObject clip;
    clip["operation"] = (int)ClipboardOpType::CopyFile;
    clip["is_cut"] = p_isCut;
    clip["sources"] = p_files;

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(QJsonDocument(clip).toJson(QJsonDocument::Compact));
}

void VFileList::pasteFilesInCurDir()
{
    pasteFiles(m_directory);
}

void VFileList::pasteFiles(VDirectory *p_destDir)
{
    qDebug() << "paste files to" << p_destDir->getName();
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    QJsonObject clip = QJsonDocument::fromJson(text.toLocal8Bit()).object();
    Q_ASSERT(!clip.isEmpty() && clip["operation"] == (int)ClipboardOpType::CopyFile);
    bool isCut = clip["is_cut"].toBool();

    int nrPasted = 0;
    for (int i = 0; i < m_copiedFiles.size(); ++i) {
        QPointer<VFile> srcFile = m_copiedFiles[i];
        if (!srcFile) {
            continue;
        }
        QString fileName = srcFile->getName();
        VDirectory *srcDir = srcFile->getDirectory();
        if (srcDir == p_destDir && !isCut) {
            // Copy and paste in the same directory.
            // Rename it to xx_copy.md
            fileName = VUtils::generateCopiedFileName(srcDir->retrivePath(), fileName);
        }
        if (copyFile(p_destDir, fileName, srcFile, isCut)) {
            nrPasted++;
        }
    }

    qDebug() << "pasted" << nrPasted << "files sucessfully";
    clipboard->clear();
    m_copiedFiles.clear();
}

bool VFileList::copyFile(VDirectory *p_destDir, const QString &p_destName, VFile *p_file, bool p_cut)
{
    QString srcPath = QDir::cleanPath(p_file->retrivePath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->retrivePath()).filePath(p_destName));
    if (srcPath == destPath) {
        return true;
    }
    // If change the file type, we need to close it first
    DocType docType = p_file->getDocType();
    DocType newDocType = VUtils::isMarkdown(destPath) ? DocType::Markdown : DocType::Html;
    if (docType != newDocType) {
        if (editArea->isFileOpened(p_file)) {
            int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                          QString("The renaming will change the note type."),
                                          QString("You should close the note %1 before continue.").arg(p_file->getName()),
                                          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok, this);
            if (QMessageBox::Ok == ret) {
                if (!editArea->closeFile(p_file, false)) {
                    return false;
                }
            } else {
                return false;
            }
        }
    }

    VFile *destFile = VDirectory::copyFile(p_destDir, p_destName, p_file, p_cut);
    updateFileList();
    if (destFile) {
        emit fileUpdated(destFile);
    }
    return destFile != NULL;
}

void VFileList::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return) {
        QListWidgetItem *item = fileList->currentItem();
        if (item) {
            handleItemClicked(item);
        }
    }
    QWidget::keyPressEvent(event);
}

bool VFileList::locateFile(const VFile *p_file)
{
    if (p_file) {
        if (p_file->getDirectory() != m_directory) {
            return false;
        }
        QListWidgetItem *item = findItem(p_file);
        if (item) {
            fileList->setCurrentItem(item);
        }
    }
    return false;
}

void VFileList::handleRowsMoved(const QModelIndex &p_parent, int p_start, int p_end, const QModelIndex &p_destination, int p_row)
{
    if (p_parent == p_destination) {
        // Items[p_start, p_end] are moved to p_row.
        m_directory->reorderFiles(p_start, p_end, p_row);
        Q_ASSERT(identicalListWithDirectory());
    }
}

bool VFileList::identicalListWithDirectory() const
{
    const QVector<VFile *> files = m_directory->getFiles();
    int nrItems = fileList->count();
    if (nrItems != files.size()) {
        return false;
    }
    for (int i = 0; i < nrItems; ++i) {
        if (getVFile(fileList->item(i)) != files.at(i)) {
            return false;
        }
    }
    return true;
}
