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
            this, &VFileList::deleteFile);

    fileInfoAct = new QAction(QIcon(":/resources/icons/note_info.svg"),
                              tr("&Info"), this);
    fileInfoAct->setStatusTip(tr("View and edit current note's information"));
    connect(fileInfoAct, &QAction::triggered,
            this, &VFileList::curFileInfo);
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
    QString defaultName = VUtils::directoryNameFromPath(p_relativePath);
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
            renameFile(p_notebook, p_relativePath, name);
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

void VFileList::deleteFile()
{
    QListWidgetItem *curItem = fileList->currentItem();
    Q_ASSERT(curItem);
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                       QString("Are you sure you want to delete note \"%1\"?")
                       .arg(curItemName), QMessageBox::Ok | QMessageBox::Cancel,
                       this);
    msgBox.setInformativeText(tr("This may be not recoverable."));
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        // First close this file forcely
        curItemJson["notebook"] = notebook;
        curItemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(curItemName));
        curItemJson["is_forced"] = true;
        emit fileDeleted(curItemJson);

        deleteFileAndUpdateList(curItem);
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

    // Update current directory's config file to include this new file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject fileJson;
    fileJson["name"] = name;
    QJsonArray fileArray = dirJson["files"].toArray();
    fileArray.push_front(fileJson);
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to add a new file"
                   << name;
        file.remove();
        return NULL;
    }

    return insertFileListItem(fileJson, true);
}

void VFileList::deleteFileAndUpdateList(QListWidgetItem *item)
{
    Q_ASSERT(item);
    QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
    QString path = QDir(rootPath).filePath(relativePath);
    QString fileName = itemJson["name"].toString();
    QString filePath = QDir(path).filePath(fileName);

    // Update current directory's config file to exclude this file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonArray fileArray = dirJson["files"].toArray();
    bool deleted = false;
    for (int i = 0; i < fileArray.size(); ++i) {
        QJsonObject ele = fileArray[i].toObject();
        if (ele["name"].toString() == fileName) {
            fileArray.removeAt(i);
            deleted = true;
            break;
        }
    }
    if (!deleted) {
        qWarning() << "error: fail to find" << fileName << "to delete";
        return;
    }
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to delete"
                   << fileName;
        return;
    }

    // Delete the file
    QFile file(filePath);
    if (!file.remove()) {
        qWarning() << "error: fail to delete" << filePath;
    } else {
        qDebug() << "delete" << filePath;
    }

    removeFileListItem(item);
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

// @p_relativePath contains the flie name
void VFileList::renameFile(const QString &p_notebook,
                           const QString &p_relativePath, const QString &p_newName)
{
    QString name = VUtils::fileNameFromPath(p_relativePath);

    // If change the file type, we need to convert it
    DocType docType = VUtils::isMarkdown(name) ? DocType::Markdown : DocType::Html;
    DocType newDocType = VUtils::isMarkdown(p_newName) ? DocType::Markdown : DocType::Html;
    if (docType != newDocType) {
        if (editArea->isFileOpened(p_notebook, p_relativePath)) {
            QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Rename will change the note type"),
                               QMessageBox::Ok | QMessageBox::Cancel, this);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.setInformativeText(QString("You should close the note %1 before continue").arg(name));
            if (QMessageBox::Ok == msgBox.exec()) {
                QJsonObject curItemJson;
                curItemJson["notebook"] = p_notebook;
                curItemJson["relative_path"] = p_relativePath;
                curItemJson["is_forced"] = false;
                if (!editArea->closeFile(curItemJson)) {
                    return;
                }
            } else {
                return;
            }
        }
        convertFileType(p_notebook, p_relativePath, docType, newDocType);
    }

    QString path = QDir(vnote->getNotebookPath(p_notebook)).filePath(VUtils::basePathFromPath(p_relativePath));
    QFile file(QDir(path).filePath(name));
    QString newFilePath(QDir(path).filePath(p_newName));
    Q_ASSERT(file.exists());
    if (!file.rename(newFilePath)) {
        qWarning() << "error: fail to rename file" << name << "under" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not rename note \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a file named \"%1\".").arg(p_newName));
        msgBox.exec();
        return;
    }

    // Update directory's config file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonArray fileArray = dirJson["files"].toArray();
    int index = 0;
    for (index = 0; index < fileArray.size(); ++index) {
        QJsonObject tmp = fileArray[index].toObject();
        if (tmp["name"].toString() == name) {
            tmp["name"] = p_newName;
            fileArray[index] = tmp;
            break;
        }
    }
    Q_ASSERT(index != fileArray.size());
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to rename file"
                   << name << "to" << p_newName;
        file.rename(name);
        return;
    }

    // Update item
    QListWidgetItem *item = findItem(p_notebook, p_relativePath);
    if (item) {
        QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
        itemJson["name"] = p_newName;
        item->setData(Qt::UserRole, itemJson);
        item->setText(p_newName);
    }

    QString oldPath = QDir::cleanPath(p_relativePath);
    QString newPath = QDir::cleanPath(QDir(VUtils::basePathFromPath(p_relativePath)).filePath(p_newName));
    qDebug() << "file renamed" << oldPath << "to" << newPath;
    emit fileRenamed(p_notebook, oldPath, newPath);
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
