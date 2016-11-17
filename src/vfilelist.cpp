#include <QtDebug>
#include <QtWidgets>
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"
#include "dialog/vfileinfodialog.h"
#include "vnote.h"
#include "veditarea.h"
#include "utils/vutils.h"

VFileList::VFileList(VNote *vnote, QWidget *parent)
    : QWidget(parent), vnote(vnote)
{
    setupUI();
    initActions();
}

void VFileList::setupUI()
{
    fileList = new QListWidget(this);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(fileList);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    connect(fileList, &QListWidget::customContextMenuRequested,
            this, &VFileList::contextMenuRequested);
    connect(fileList, &QListWidget::itemClicked,
            this, &VFileList::handleItemClicked);

    setLayout(mainLayout);
}

void VFileList::initActions()
{
    newFileAct = new QAction(QIcon(":/resources/icons/create_note.svg"),
                             tr("&New note"), this);
    newFileAct->setStatusTip(tr("Create a new note in current directory"));
    connect(newFileAct, SIGNAL(triggered(bool)),
            this, SLOT(newFile()));

    deleteFileAct = new QAction(QIcon(":/resources/icons/delete_note.svg"),
                                tr("&Delete"), this);
    deleteFileAct->setStatusTip(tr("Delete selected note"));
    connect(deleteFileAct, &QAction::triggered,
            this, &VFileList::deleteCurFile);

    fileInfoAct = new QAction(QIcon(":/resources/icons/note_info.svg"),
                              tr("&Info"), this);
    fileInfoAct->setStatusTip(tr("View and edit current note's information"));
    connect(fileInfoAct, &QAction::triggered,
            this, &VFileList::curFileInfo);

    copyAct = new QAction(QIcon(":/resources/icons/copy.svg"),
                          tr("&Copy"), this);
    copyAct->setStatusTip(tr("Copy selected notes"));
    connect(copyAct, &QAction::triggered,
            this, &VFileList::copySelectedFiles);

    cutAct = new QAction(QIcon(":/resources/icons/cut.svg"),
                          tr("&Cut"), this);
    cutAct->setStatusTip(tr("Cut selected notes"));
    connect(cutAct, &QAction::triggered,
            this, &VFileList::cutSelectedFiles);

    pasteAct = new QAction(QIcon(":/resources/icons/paste.svg"),
                          tr("&Paste"), this);
    pasteAct->setStatusTip(tr("Paste notes"));
    connect(pasteAct, &QAction::triggered,
            this, &VFileList::pasteFilesInCurDir);
}

void VFileList::setDirectory(QJsonObject dirJson)
{
    fileList->clear();
    if (dirJson.isEmpty()) {
        clearDirectoryInfo();
        emit directoryChanged("", "");
        return;
    }

    notebook = dirJson["notebook"].toString();
    relativePath = dirJson["relative_path"].toString();
    rootPath = "";
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == notebook) {
            rootPath = notebooks[i].getPath();
            break;
        }
    }
    Q_ASSERT(!rootPath.isEmpty());

    updateFileList();

    emit directoryChanged(notebook, relativePath);
}

void VFileList::clearDirectoryInfo()
{
    notebook = relativePath = rootPath = "";
}

void VFileList::updateFileList()
{
    QString path = QDir(rootPath).filePath(relativePath);

    fileList->clear();
    if (!QDir(path).exists()) {
        qDebug() << "invalid notebook directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook directory \"%1\" either does not exist or is not valid.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qDebug() << "invalid notebook configuration for directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory configuration."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook directory \"%1\" does not contain a valid configuration file.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    // Handle files section
    QJsonArray filesJson = configJson["files"].toArray();
    for (int i = 0; i < filesJson.size(); ++i) {
        QJsonObject fileItem = filesJson[i].toObject();
        insertFileListItem(fileItem);
    }
}

void VFileList::curFileInfo()
{
    QListWidgetItem *curItem = fileList->currentItem();
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!curItemJson.isEmpty());
    QString curItemName = curItemJson["name"].toString();
    fileInfo(notebook, QDir(relativePath).filePath(curItemName));
}

void VFileList::fileInfo(const QString &p_notebook, const QString &p_relativePath)
{
    qDebug() << "fileInfo" << p_notebook << p_relativePath;
    QString info;
    QString defaultName = VUtils::fileNameFromPath(p_relativePath);
    QString curName = defaultName;
    do {
        VFileInfoDialog dialog(tr("Note Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (isConflictNameWithExisting(name)) {
                info = "Name already exists.\nPlease choose another name:";
                defaultName = name;
                continue;
            }
            copyFile(p_notebook, p_relativePath, p_notebook,
                     QDir(VUtils::basePathFromPath(p_relativePath)).filePath(name), true);
        }
        break;
    } while (true);
}

QListWidgetItem* VFileList::insertFileListItem(QJsonObject fileJson, bool atFront)
{
    Q_ASSERT(!fileJson.isEmpty());
    QListWidgetItem *item = new QListWidgetItem(fileJson["name"].toString());
    item->setData(Qt::UserRole, fileJson);

    if (atFront) {
        fileList->insertItem(0, item);
    } else {
        fileList->addItem(item);
    }
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    qDebug() << "add new list item:" << fileJson["name"].toString();
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
    QString text("&Note name:");
    QString defaultText("new_note");
    do {
        VNewFileDialog dialog(QString("Create a new note under %1").arg(VUtils::directoryNameFromPath(relativePath)),
                              text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (isConflictNameWithExisting(name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                continue;
            }
            QListWidgetItem *newItem = createFileAndUpdateList(name);
            if (newItem) {
                fileList->setCurrentItem(newItem);
                // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
                fileList->update();

                // Open this file in edit mode
                QJsonObject itemJson = newItem->data(Qt::UserRole).toJsonObject();
                Q_ASSERT(!itemJson.isEmpty());
                itemJson["notebook"] = notebook;
                itemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(name));
                itemJson["mode"] = OpenFileMode::Edit;
                emit fileCreated(itemJson);
            }
        }
        break;
    } while (true);
}

void VFileList::deleteCurFile()
{
    QListWidgetItem *curItem = fileList->currentItem();
    Q_ASSERT(curItem);
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();
    deleteFile(notebook, QDir(relativePath).filePath(curItemName));
}

// @p_relativePath contains the file name
void VFileList::deleteFile(const QString &p_notebook, const QString &p_relativePath)
{
    QString fileName = VUtils::fileNameFromPath(p_relativePath);
    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                       QString("Are you sure you want to delete note \"%1\"?")
                       .arg(fileName), QMessageBox::Ok | QMessageBox::Cancel,
                       this);
    msgBox.setInformativeText(tr("This may be not recoverable."));
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        // First close this file forcely
        QJsonObject curItemJson;
        curItemJson["notebook"] = p_notebook;
        curItemJson["relative_path"] = QDir::cleanPath(p_relativePath);
        curItemJson["is_forced"] = true;
        emit fileDeleted(curItemJson);

        deleteFileAndUpdateList(p_notebook, p_relativePath);
    }
}

void VFileList::contextMenuRequested(QPoint pos)
{
    QListWidgetItem *item = fileList->itemAt(pos);
    QMenu menu(this);

    if (notebook.isEmpty()) {
        return;
    }
    menu.addAction(newFileAct);
    if (item) {
        menu.addAction(deleteFileAct);
        menu.addAction(fileInfoAct);
        menu.addAction(copyAct);
        menu.addAction(cutAct);
    }

    if (VUtils::opTypeInClipboard() == ClipboardOpType::CopyFile) {
        menu.addAction(pasteAct);
    }
    menu.exec(fileList->mapToGlobal(pos));
}

bool VFileList::isConflictNameWithExisting(const QString &name)
{
    int nrChild = fileList->count();
    for (int i = 0; i < nrChild; ++i) {
        QListWidgetItem *item = fileList->item(i);
        QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
        Q_ASSERT(!itemJson.isEmpty());
        if (itemJson["name"].toString() == name) {
            return true;
        }
    }
    return false;
}

QListWidgetItem* VFileList::findItem(const QString &p_notebook, const QString &p_relativePath)
{
    if (p_notebook != notebook || VUtils::basePathFromPath(p_relativePath) != QDir::cleanPath(relativePath)) {
        return NULL;
    }
    QString name = VUtils::fileNameFromPath(p_relativePath);
    int nrChild = fileList->count();
    for (int i = 0; i < nrChild; ++i) {
        QListWidgetItem *item = fileList->item(i);
        QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
        Q_ASSERT(!itemJson.isEmpty());
        if (itemJson["name"].toString() == name) {
            return item;
        }
    }
    return NULL;
}

QListWidgetItem* VFileList::createFileAndUpdateList(const QString &name)
{
    QString path = QDir(rootPath).filePath(relativePath);
    QString filePath = QDir(path).filePath(name);
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to create file:" << filePath;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not create file \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a file named \"%1\".").arg(name));
        msgBox.exec();
        return NULL;
    }
    file.close();
    qDebug() << "create file:" << filePath;

    if (!addFileInConfig(filePath, 0)) {
        file.remove();
        return NULL;
    }

    return insertFileListItem(readFileInConfig(filePath), true);
}

void VFileList::deleteFileAndUpdateList(const QString &p_notebook,
                                        const QString &p_relativePath)
{
    QString filePath = QDir(vnote->getNotebookPath(p_notebook)).filePath(p_relativePath);

    if (!removeFileInConfig(filePath)) {
        return;
    }

    // Delete local images in ./images
    deleteLocalImages(filePath);

    // Delete the file
    QFile file(filePath);
    if (!file.remove()) {
        qWarning() << "error: fail to delete" << filePath;
    } else {
        qDebug() << "delete" << filePath;
    }

    QListWidgetItem *item = findItem(p_notebook, p_relativePath);
    if (item) {
        removeFileListItem(item);
    }
}

void VFileList::handleItemClicked(QListWidgetItem *currentItem)
{
    if (!currentItem) {
        emit fileClicked(QJsonObject());
        return;
    }
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    QJsonObject itemJson = currentItem->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    itemJson["notebook"] = notebook;
    itemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(itemJson["name"].toString()));
    itemJson["mode"] = OpenFileMode::Read;
    emit fileClicked(itemJson);
}

bool VFileList::importFile(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (isConflictNameWithExisting(name)) {
        return false;
    }

    // Copy file @name to current directory
    QString targetPath = QDir(rootPath).filePath(relativePath);
    QString srcName = QFileInfo(name).fileName();
    if (srcName.isEmpty()) {
        return false;
    }
    QString targetName = QDir(targetPath).filePath(srcName);

    bool ret = QFile::copy(name, targetName);
    if (!ret) {
        qWarning() << "error: fail to copy" << name << "to" << targetName;
        return false;
    }

    // Update current directory's config file to include this new file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(targetPath);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject fileJson;
    fileJson["name"] = srcName;
    QJsonArray fileArray = dirJson["files"].toArray();
    fileArray.push_front(fileJson);
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(targetPath, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to add a new file"
                   << srcName;
        QFile(targetName).remove();
        return false;
    }

    return insertFileListItem(fileJson, true);
}

void VFileList::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                      const QString &oldName, const QString &newName)
{
    if (oldName == notebook) {
        notebook = newName;
    }
}

void VFileList::handleDirectoryRenamed(const QString &notebook,
                                       const QString &oldRelativePath, const QString &newRelativePath)
{
    if (notebook == this->notebook
        && relativePath.startsWith(oldRelativePath)) {
        relativePath.replace(0, oldRelativePath.size(), newRelativePath);
    }
}

void VFileList::convertFileType(const QString &notebook, const QString &fileRelativePath,
                                DocType oldType, DocType newType)
{
    Q_ASSERT(oldType != newType);
    QString filePath = QDir(vnote->getNotebookPath(notebook)).filePath(fileRelativePath);
    QString fileText = VUtils::readFileFromDisk(filePath);
    QTextEdit editor;
    if (oldType == DocType::Markdown) {
        editor.setPlainText(fileText);
        fileText = editor.toHtml();
    } else {
        editor.setHtml(fileText);
        fileText = editor.toPlainText();
    }
    VUtils::writeFileToDisk(filePath, fileText);
}

void VFileList::deleteLocalImages(const QString &filePath)
{
    if (!VUtils::isMarkdown(filePath)) {
        return;
    }

    QVector<QString> images = VUtils::imagesFromMarkdownFile(filePath);
    int deleted = 0;
    for (int i = 0; i < images.size(); ++i) {
        QFile file(images[i]);
        if (file.remove()) {
            ++deleted;
        }
    }
    qDebug() << "delete" << deleted << "images for" << filePath;
}

void VFileList::copySelectedFiles(bool p_isCut)
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QJsonArray files;
    QDir dir(relativePath);
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject itemJson = items[i]->data(Qt::UserRole).toJsonObject();
        QString itemName = itemJson["name"].toString();
        QJsonObject fileJson;
        fileJson["notebook"] = notebook;
        fileJson["relative_path"] = dir.filePath(itemName);
        files.append(fileJson);
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
    pasteFiles(notebook, relativePath);
}

void VFileList::pasteFiles(const QString &p_notebook, const QString &p_dirRelativePath)
{
    qDebug() << "paste files to" << p_notebook << p_dirRelativePath;
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    QJsonObject clip = QJsonDocument::fromJson(text.toLocal8Bit()).object();
    Q_ASSERT(!clip.isEmpty() && clip["operation"] == (int)ClipboardOpType::CopyFile);

    bool isCut = clip["is_cut"].toBool();
    QJsonArray sources = clip["sources"].toArray();

    int nrFiles = sources.size();
    QDir destDir(p_dirRelativePath);
    int nrPasted = 0;
    for (int i = 0; i < nrFiles; ++i) {
        QJsonObject file = sources[i].toObject();
        QString srcNotebook = file["notebook"].toString();
        QString srcRelativePath = file["relative_path"].toString();
        bool ret = copyFile(srcNotebook, srcRelativePath, p_notebook,
                            destDir.filePath(VUtils::fileNameFromPath(srcRelativePath)), isCut);
        if (ret) {
            nrPasted++;
        }
    }
    qDebug() << "pasted" << nrPasted << "files sucessfully";
    clipboard->clear();
}

bool VFileList::copyFile(const QString &p_srcNotebook, const QString &p_srcRelativePath,
                         const QString &p_destNotebook, const QString &p_destRelativePath,
                         bool p_isCut)
{
    QString srcPath = QDir(vnote->getNotebookPath(p_srcNotebook)).filePath(p_srcRelativePath);
    srcPath = QDir::cleanPath(srcPath);
    QString destPath = QDir(vnote->getNotebookPath(p_destNotebook)).filePath(p_destRelativePath);
    destPath = QDir::cleanPath(destPath);
    if (srcPath == destPath) {
        return true;
    }

    // If change the file type, we need to convert it
    bool needConversion = false;
    DocType docType = VUtils::isMarkdown(srcPath) ? DocType::Markdown : DocType::Html;
    DocType newDocType = VUtils::isMarkdown(destPath) ? DocType::Markdown : DocType::Html;
    if (docType != newDocType) {
        if (editArea->isFileOpened(p_srcNotebook, p_srcRelativePath)) {
            QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Rename will change the note type"),
                               QMessageBox::Ok | QMessageBox::Cancel, this);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.setInformativeText(QString("You should close the note %1 before continue")
                                      .arg(VUtils::fileNameFromPath(p_srcRelativePath)));
            if (QMessageBox::Ok == msgBox.exec()) {
                QJsonObject curItemJson;
                curItemJson["notebook"] = p_srcNotebook;
                curItemJson["relative_path"] = p_srcRelativePath;
                curItemJson["is_forced"] = false;
                if (!editArea->closeFile(curItemJson)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        // Convert it later
        needConversion = true;
    }

    QVector<QString> images;
    if (docType == DocType::Markdown) {
        images = VUtils::imagesFromMarkdownFile(srcPath);
    }

    // Copy the file
    if (!VUtils::copyFile(srcPath, destPath, p_isCut)) {
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Fail to copy %1 from %2.")
                           .arg(p_srcRelativePath).arg(p_srcNotebook), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a file with the same name"));
        msgBox.exec();
        return false;
    }

    if (needConversion) {
        convertFileType(p_destNotebook, p_destRelativePath, docType, newDocType);
    }

    // We need to copy images when it is still markdown
    if (!images.isEmpty()) {
        if (newDocType == DocType::Markdown) {
            QString dirPath = QDir(VUtils::basePathFromPath(destPath)).filePath("images");
            VUtils::makeDirectory(dirPath);
            int nrPasted = 0;
            for (int i = 0; i < images.size(); ++i) {
                if (!QFile(images[i]).exists()) {
                    continue;
                }

                QString destImagePath = QDir(dirPath).filePath(VUtils::fileNameFromPath(images[i]));
                if (VUtils::copyFile(images[i], destImagePath, p_isCut)) {
                    nrPasted++;
                } else {
                    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Fail to copy image %1.")
                                       .arg(images[i]), QMessageBox::Ok, this);
                    msgBox.setInformativeText(QString("Please check if there already exists a file with the same name and manually copy it"));
                    msgBox.exec();
                }
            }
            qDebug() << "pasted" << nrPasted << "images sucessfully";
        } else {
            // Delete the images
            for (int i = 0; i < images.size(); ++i) {
                QFile file(images[i]);
                file.remove();
            }
        }
    }

    int idx = -1;
    if (p_isCut) {
        // Remove src in the config
        idx = removeFileInConfig(srcPath);
        if (VUtils::basePathFromPath(srcPath) != VUtils::basePathFromPath(destPath)) {
            idx = -1;
        }
    }

    // Add dest in the config
    addFileInConfig(destPath, idx);

    updateFileList();

    if (p_isCut) {
        emit fileRenamed(p_srcNotebook, p_srcRelativePath,
                         p_destNotebook, p_destRelativePath);
    }
    return true;
}

int VFileList::removeFileInConfig(const QString &p_filePath)
{
    QString dirPath = VUtils::basePathFromPath(p_filePath);
    QString fileName = VUtils::fileNameFromPath(p_filePath);
    // Update current directory's config file to exclude this file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(dirPath);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonArray fileArray = dirJson["files"].toArray();
    bool deleted = false;
    int idx = -1;
    for (int i = 0; i < fileArray.size(); ++i) {
        QJsonObject ele = fileArray[i].toObject();
        if (ele["name"].toString() == fileName) {
            fileArray.removeAt(i);
            deleted = true;
            idx = i;
            break;
        }
    }
    if (!deleted) {
        qWarning() << "error: fail to find" << fileName << "to delete";
        return idx;
    }
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(dirPath, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to delete"
                   << fileName;
        return idx;
    }
    return idx;
}

// @index = -1, add it to the end of the list
bool VFileList::addFileInConfig(const QString &p_filePath, int p_index)
{
    QString dirPath = VUtils::basePathFromPath(p_filePath);
    QString fileName = VUtils::fileNameFromPath(p_filePath);

    // Update current directory's config file to include this file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(dirPath);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject fileJson;
    fileJson["name"] = fileName;
    QJsonArray fileArray = dirJson["files"].toArray();
    if (p_index == -1) {
        p_index = fileArray.size();
    }
    fileArray.insert(p_index, fileJson);
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(dirPath, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to add a new file"
                   << fileName;
        return false;
    }

    return true;
}

QJsonObject VFileList::readFileInConfig(const QString &p_filePath)
{
    QString dirPath = VUtils::basePathFromPath(p_filePath);
    QString fileName = VUtils::fileNameFromPath(p_filePath);

    QJsonObject dirJson = VConfigManager::readDirectoryConfig(dirPath);
    Q_ASSERT(!dirJson.isEmpty());

    qDebug() << "config" << p_filePath;
    QJsonArray fileArray = dirJson["files"].toArray();
    for (int i = 0; i < fileArray.size(); ++i) {
        QJsonObject ele = fileArray[i].toObject();
        if (ele["name"].toString() == fileName) {
            return ele;
        }
    }
    return QJsonObject();
}
