#include <QtDebug>
#include <QtWidgets>
#include <QUrl>
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"
#include "dialog/vfileinfodialog.h"
#include "vnote.h"
#include "veditarea.h"
#include "utils/vutils.h"
#include "vfile.h"
#include "vconfigmanager.h"
#include "vmdedit.h"

extern VConfigManager *g_config;
extern VNote *g_vnote;

const QString VFileList::c_infoShortcutSequence = "F2";
const QString VFileList::c_copyShortcutSequence = "Ctrl+C";
const QString VFileList::c_cutShortcutSequence = "Ctrl+X";
const QString VFileList::c_pasteShortcutSequence = "Ctrl+V";

VFileList::VFileList(QWidget *parent)
    : QWidget(parent), VNavigationMode()
{
    setupUI();
    initShortcuts();
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

void VFileList::initShortcuts()
{
    QShortcut *infoShortcut = new QShortcut(QKeySequence(c_infoShortcutSequence), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, [this](){
                fileInfo();
            });

    QShortcut *copyShortcut = new QShortcut(QKeySequence(c_copyShortcutSequence), this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated,
            this, [this](){
                copySelectedFiles();
            });

    QShortcut *cutShortcut = new QShortcut(QKeySequence(c_cutShortcutSequence), this);
    cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cutShortcut, &QShortcut::activated,
            this, [this](){
                cutSelectedFiles();
            });

    QShortcut *pasteShortcut = new QShortcut(QKeySequence(c_pasteShortcutSequence), this);
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated,
            this, [this](){
                pasteFilesInCurDir();
            });
}

void VFileList::initActions()
{
    newFileAct = new QAction(QIcon(":/resources/icons/create_note.svg"),
                             tr("&New Note"), this);
    QString shortcutStr = VUtils::getShortcutText(g_config->getShortcutKeySequence("NewNote"));
    if (!shortcutStr.isEmpty()) {
        newFileAct->setText(tr("&New Note\t%1").arg(shortcutStr));
    }

    newFileAct->setToolTip(tr("Create a note in current folder"));
    connect(newFileAct, SIGNAL(triggered(bool)),
            this, SLOT(newFile()));

    deleteFileAct = new QAction(QIcon(":/resources/icons/delete_note.svg"),
                                tr("&Delete"), this);
    deleteFileAct->setToolTip(tr("Delete selected note"));
    connect(deleteFileAct, SIGNAL(triggered(bool)),
            this, SLOT(deleteFile()));

    fileInfoAct = new QAction(QIcon(":/resources/icons/note_info.svg"),
                              tr("&Info\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)), this);
    fileInfoAct->setToolTip(tr("View and edit current note's information"));
    connect(fileInfoAct, SIGNAL(triggered(bool)),
            this, SLOT(fileInfo()));

    copyAct = new QAction(QIcon(":/resources/icons/copy.svg"),
                          tr("&Copy\t%1").arg(VUtils::getShortcutText(c_copyShortcutSequence)), this);
    copyAct->setToolTip(tr("Copy selected notes"));
    connect(copyAct, &QAction::triggered,
            this, &VFileList::copySelectedFiles);

    cutAct = new QAction(QIcon(":/resources/icons/cut.svg"),
                         tr("C&ut\t%1").arg(VUtils::getShortcutText(c_cutShortcutSequence)), this);
    cutAct->setToolTip(tr("Cut selected notes"));
    connect(cutAct, &QAction::triggered,
            this, &VFileList::cutSelectedFiles);

    pasteAct = new QAction(QIcon(":/resources/icons/paste.svg"),
                           tr("&Paste\t%1").arg(VUtils::getShortcutText(c_pasteShortcutSequence)), this);
    pasteAct->setToolTip(tr("Paste notes in current folder"));
    connect(pasteAct, &QAction::triggered,
            this, &VFileList::pasteFilesInCurDir);

    m_openLocationAct = new QAction(tr("&Open Note Location"), this);
    m_openLocationAct->setToolTip(tr("Open the folder containing this note in operating system"));
    connect(m_openLocationAct, &QAction::triggered,
            this, &VFileList::openFileLocation);
}

void VFileList::setDirectory(VDirectory *p_directory)
{
    // QPointer will be set to NULL automatically once the directory was deleted.
    // If the last directory is deleted, m_directory and p_directory will both
    // be NULL.
    if (m_directory == p_directory) {
        if (!m_directory) {
            fileList->clear();
        }

        return;
    }

    m_directory = p_directory;
    if (!m_directory) {
        fileList->clear();
        return;
    }

    qDebug() << "filelist set folder" << m_directory->getName();
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
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.size() == 1) {
        fileInfo(getVFile(items[0]));
    }
}

void VFileList::openFileLocation() const
{
    QListWidgetItem *curItem = fileList->currentItem();
    V_ASSERT(curItem);
    QUrl url = QUrl::fromLocalFile(getVFile(curItem)->fetchBasePath());
    QDesktopServices::openUrl(url);
}

void VFileList::fileInfo(VFile *p_file)
{
    if (!p_file) {
        return;
    }

    VDirectory *dir = p_file->getDirectory();
    QString curName = p_file->getName();
    VFileInfoDialog dialog(tr("Note Information"), "", dir, p_file, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getNameInput();
        if (name == curName) {
            return;
        }

        if (!promptForDocTypeChange(p_file, QDir(p_file->fetchBasePath()).filePath(name))) {
            return;
        }

        if (!p_file->rename(name)) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to rename note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(curName), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        QListWidgetItem *item = findItem(p_file);
        if (item) {
            fillItem(item, p_file);
        }

        emit fileUpdated(p_file);
    }
}

void VFileList::fillItem(QListWidgetItem *p_item, const VFile *p_file)
{
    unsigned long long ptr = (long long)p_file;
    p_item->setData(Qt::UserRole, ptr);
    p_item->setToolTip(p_file->getName());
    p_item->setText(p_file->getName());

    V_ASSERT(sizeof(p_file) <= sizeof(ptr));
}

QListWidgetItem* VFileList::insertFileListItem(VFile *file, bool atFront)
{
    V_ASSERT(file);
    QListWidgetItem *item = new QListWidgetItem();
    fillItem(item, file);

    if (atFront) {
        fileList->insertItem(0, item);
    } else {
        fileList->addItem(item);
    }

    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    qDebug() << "VFileList adds" << file->getName();
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

    QList<QString> suffixes = g_config->getDocSuffixes()[(int)DocType::Markdown];
    QString defaultSuf;
    QString suffixStr;
    for (auto const & suf : suffixes) {
        suffixStr += (suffixStr.isEmpty() ? suf : "/" + suf);
        if (defaultSuf.isEmpty() || suf == "md") {
            defaultSuf = suf;
        }
    }

    QString info = tr("Create a note in <span style=\"%1\">%2</span>.")
                     .arg(g_config->c_dataTextStyle).arg(m_directory->getName());
    info = info + "<br>" + tr("Note with name ending with \"%1\" will be treated as Markdown type.")
                             .arg(suffixStr);
    QString defaultName = QString("new_note.%1").arg(defaultSuf);
    defaultName = VUtils::getFileNameWithSequence(m_directory->fetchPath(), defaultName);
    VNewFileDialog dialog(tr("Create Note"), info, defaultName, m_directory, this);
    if (dialog.exec() == QDialog::Accepted) {
        VFile *file = m_directory->createFile(dialog.getNameInput());
        if (!file) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to create note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(dialog.getNameInput()), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        // Write title if needed.
        bool contentInserted = false;
        if (dialog.getInsertTitleInput() && file->getDocType() == DocType::Markdown) {
            if (!file->open()) {
                qWarning() << "fail to open newly-created note" << file->getName();
            } else {
                Q_ASSERT(file->getContent().isEmpty());
                QString content = QString("# %1\n").arg(QFileInfo(file->getName()).baseName());
                file->setContent(content);
                if (!file->save()) {
                    qWarning() << "fail to write to newly-created note" << file->getName();
                } else {
                    contentInserted = true;
                }

                file->close();
            }
        }

        QVector<QListWidgetItem *> items = updateFileListAdded();
        Q_ASSERT(items.size() == 1);
        fileList->setCurrentItem(items[0], QItemSelectionModel::ClearAndSelect);
        // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
        fileList->update();

        // Open it in edit mode
        emit fileCreated(file, OpenFileMode::Edit);

        // Move cursor down if content has been inserted.
        if (contentInserted) {
            QWidget *wid = QApplication::focusWidget();
            VMdEdit *edit = dynamic_cast<VMdEdit *>(wid);
            if (edit && edit->getFile() == file) {
                QKeyEvent *downEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down,
                                                     Qt::NoModifier);
                QCoreApplication::postEvent(edit, downEvent);
            }
        }
    }
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
                                  tr("Are you sure to delete note <span style=\"%1\">%2</span>?")
                                    .arg(g_config->c_dataTextStyle).arg(fileName),
                                  tr("<span style=\"%1\">WARNING</span>: The files (including images) "
                                     "deleted may be UNRECOVERABLE!")
                                    .arg(g_config->c_warningTextStyle),
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Ok, this, MessageBoxType::Danger);
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
    menu.setToolTipsVisible(true);

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

    if (item) {
        menu.addSeparator();
        menu.addAction(m_openLocationAct);

        if (fileList->selectedItems().size() == 1) {
            menu.addAction(fileInfoAct);
        }
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
    Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    if (modifiers != Qt::NoModifier) {
        return;
    }

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
    QString targetPath = m_directory->fetchPath();
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
        fileJson["path"] = file->fetchPath();
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
    if (m_copiedFiles.isEmpty()) {
        return;
    }

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
            fileName = VUtils::generateCopiedFileName(srcDir->fetchPath(), fileName);
        }
        if (copyFile(p_destDir, fileName, srcFile, isCut)) {
            nrPasted++;
        } else {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to copy note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(srcFile->getName()),
                                tr("Please check if there already exists a file with the same name in the target folder."),
                                QMessageBox::Ok, QMessageBox::Ok, this);
        }
    }

    qDebug() << "pasted" << nrPasted << "files sucessfully";
    clipboard->clear();
    m_copiedFiles.clear();
}

bool VFileList::copyFile(VDirectory *p_destDir, const QString &p_destName, VFile *p_file, bool p_cut)
{
    QString srcPath = QDir::cleanPath(p_file->fetchPath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->fetchPath()).filePath(p_destName));
    if (VUtils::equalPath(srcPath, destPath)) {
        return true;
    }

    // If change the file type, we need to close it first
    if (!promptForDocTypeChange(p_file, destPath)) {
        return false;
    }

    VFile *destFile = VDirectory::copyFile(p_destDir, p_destName, p_file, p_cut);
    updateFileList();
    if (destFile) {
        emit fileUpdated(destFile);
    }
    return destFile != NULL;
}

bool VFileList::promptForDocTypeChange(const VFile *p_file, const QString &p_newFilePath)
{
    DocType docType = p_file->getDocType();
    DocType newDocType = VUtils::docTypeFromName(p_newFilePath);

    if (docType != newDocType) {
        if (editArea->isFileOpened(p_file)) {
            int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                          tr("The renaming will change the note type."),
                                          tr("You should close the note <span style=\"%1\">%2</span> before continue.")
                                            .arg(g_config->c_dataTextStyle).arg(p_file->getName()),
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

    return true;
}

void VFileList::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();
    int modifiers = event->modifiers();
    switch (key) {
    case Qt::Key_Return:
    {
        QListWidgetItem *item = fileList->currentItem();
        if (item) {
            handleItemClicked(item);
        }

        break;
    }


    case Qt::Key_J:
    {
        if (modifiers == Qt::ControlModifier) {
            event->accept();
            QKeyEvent *downEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down,
                                                 Qt::NoModifier);
            QCoreApplication::postEvent(fileList, downEvent);
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
            QCoreApplication::postEvent(fileList, upEvent);
            return;
        }
        break;
    }

    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void VFileList::focusInEvent(QFocusEvent * /* p_event */)
{
    fileList->setFocus();
}

bool VFileList::locateFile(const VFile *p_file)
{
    if (p_file) {
        if (p_file->getDirectory() != m_directory) {
            return false;
        }

        QListWidgetItem *item = findItem(p_file);
        if (item) {
            fileList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            return true;
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

void VFileList::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
    V_ASSERT(m_keyMap.empty());
    V_ASSERT(m_naviLabels.empty());
}

void VFileList::showNavigation()
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
    int itemWidth = rect().width();
    for (int i = 0; i < 26 && i < items.size(); ++i) {
        QChar key('a' + i);
        m_keyMap[key] = items[i];

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, this);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->show();
        QRect rect = fileList->visualItemRect(items[i]);
        // Display the label at the end to show the file name.
        label->move(rect.x() + itemWidth - label->width(), rect.y());
        m_naviLabels.append(label);
    }
}

void VFileList::hideNavigation()
{
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }
    m_naviLabels.clear();
}

bool VFileList::handleKeyNavigation(int p_key, bool &p_succeed)
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
            fileList->setCurrentItem(it.value(), QItemSelectionModel::ClearAndSelect);
            fileList->setFocus();
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

QList<QListWidgetItem *> VFileList::getVisibleItems() const
{
    QList<QListWidgetItem *> items;
    for (int i = 0; i < fileList->count(); ++i) {
        QListWidgetItem *item = fileList->item(i);
        if (!item->isHidden()) {
            items.append(item);
        }
    }
    return items;
}

