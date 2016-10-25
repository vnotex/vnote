#include <QtWidgets>
#include "vfilelistpanel.h"
#include "vfilelist.h"
#include "dialog/vfileinfodialog.h"
#include "vnotebook.h"
#include "vnote.h"
#include "vconfigmanager.h"

VFileListPanel::VFileListPanel(VNote *vnote, QWidget *parent)
    : QWidget(parent), vnote(vnote)
{
    setupUI();
}

void VFileListPanel::setupUI()
{
    newFileBtn = new QPushButton(QIcon(":/resources/icons/create_note.png"), "");
    newFileBtn->setToolTip(tr("Create a new note"));
    deleteFileBtn = new QPushButton(QIcon(":/resources/icons/delete_note.png"), "");
    deleteFileBtn->setToolTip(tr("Delete current note"));
    fileInfoBtn = new QPushButton(QIcon(":/resources/icons/note_info.png"), "");
    fileInfoBtn->setToolTip(tr("View and edit current note's information"));

    fileList = new VFileList(vnote);

    QHBoxLayout *topLayout = new QHBoxLayout;
    topLayout->addStretch();
    topLayout->addWidget(newFileBtn);
    topLayout->addWidget(deleteFileBtn);
    topLayout->addWidget(fileInfoBtn);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(fileList);

    // Signals
    connect(fileList, &VFileList::fileClicked,
            this, &VFileListPanel::fileClicked);
    connect(fileList, &VFileList::fileDeleted,
            this, &VFileListPanel::fileDeleted);
    connect(fileList, &VFileList::fileCreated,
            this, &VFileListPanel::fileCreated);

    connect(newFileBtn, &QPushButton::clicked,
            this, &VFileListPanel::onNewFileBtnClicked);
    connect(deleteFileBtn, &QPushButton::clicked,
            this, &VFileListPanel::onDeleteFileBtnClicked);
    connect(fileInfoBtn, &QPushButton::clicked,
            this, &VFileListPanel::onFileInfoBtnClicked);

    setLayout(mainLayout);
}

void VFileListPanel::setDirectory(QJsonObject dirJson)
{
    if (dirJson.isEmpty()) {
        clearDirectoryInfo();
        fileList->setDirectory(dirJson);
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

    fileList->setDirectory(dirJson);
}

void VFileListPanel::clearDirectoryInfo()
{
    notebook = relativePath = rootPath = "";
    fileList->clear();
}

void VFileListPanel::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                           const QString &oldName, const QString &newName)
{
    if (oldName == notebook) {
        // Update treePath (though treePath actually will not be changed)
        notebook = newName;
        rootPath.clear();
        const QVector<VNotebook> &notebooks = vnote->getNotebooks();
        for (int i = 0; i < notebooks.size(); ++i) {
            if (notebooks[i].getName() == notebook) {
                rootPath = notebooks[i].getPath();
                break;
            }
        }
        Q_ASSERT(!rootPath.isEmpty());
    }
    fileList->handleNotebookRenamed(notebooks, oldName, newName);
}

void VFileListPanel::handleDirectoryRenamed(const QString &notebook,
                                            const QString &oldRelativePath, const QString &newRelativePath)
{
    if (notebook == this->notebook
        && oldRelativePath == relativePath) {
        relativePath = newRelativePath;
    }
    fileList->handleDirectoryRenamed(notebook, oldRelativePath,
                                     newRelativePath);
}

bool VFileListPanel::importFile(const QString &name)
{
    return fileList->importFile(name);
}

void VFileListPanel::onNewFileBtnClicked()
{
    if (relativePath.isEmpty()) {
        return;
    }
    fileList->newFile();
}

void VFileListPanel::onDeleteFileBtnClicked()
{
    QListWidgetItem *curItem = fileList->currentItem();
    if (!curItem) {
        return;
    }
    fileList->deleteFile();
}

void VFileListPanel::onFileInfoBtnClicked()
{
    QListWidgetItem *curItem = fileList->currentItem();
    if (!curItem) {
        return;
    }
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!curItemJson.isEmpty());
    QString curItemName = curItemJson["name"].toString();

    QString info;
    QString defaultName = curItemName;

    do {
        VFileInfoDialog dialog(tr("Note Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curItemName) {
                return;
            }
            if (isConflictNameWithExisting(name)) {
                info = "Name already exists.\nPlease choose another name:";
                defaultName = name;
                continue;
            }
            renameFile(curItem, name);
        }
        break;
    } while (true);
}

bool VFileListPanel::isConflictNameWithExisting(const QString &name)
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

void VFileListPanel::renameFile(QListWidgetItem *item, const QString &newName)
{
    if (!item) {
        return;
    }
    QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    QString name = itemJson["name"].toString();
    QString path = QDir(rootPath).filePath(relativePath);
    QFile file(QDir(path).filePath(name));
    QString newFilePath(QDir(path).filePath(newName));
    Q_ASSERT(file.exists());
    if (!file.rename(newFilePath)) {
        qWarning() << "error: fail to rename file" << name << "under" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not rename note \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a file named \"%1\".").arg(newName));
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
            tmp["name"] = newName;
            fileArray[index] = tmp;
            break;
        }
    }
    Q_ASSERT(index != fileArray.size());
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to rename file"
                   << name << "to" << newName;
        file.rename(name);
        return;
    }

    // Update item
    itemJson["name"] = newName;
    item->setData(Qt::UserRole, itemJson);
    item->setText(newName);

    QString oldPath = QDir::cleanPath(QDir(relativePath).filePath(name));
    QString newPath = QDir::cleanPath(QDir(relativePath).filePath(newName));
    qDebug() << "file renamed" << oldPath << "to" << newPath;
    emit fileRenamed(notebook, oldPath, newPath);
}
