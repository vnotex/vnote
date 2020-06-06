#include <QtDebug>
#include <QtWidgets>
#include <QUrl>
#include <QTimer>

#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"
#include "dialog/vfileinfodialog.h"
#include "vnote.h"
#include "veditarea.h"
#include "utils/vutils.h"
#include "vnotefile.h"
#include "vconfigmanager.h"
#include "vmdeditor.h"
#include "vmdtab.h"
#include "dialog/vconfirmdeletiondialog.h"
#include "dialog/vsortdialog.h"
#include "vmainwindow.h"
#include "utils/viconutils.h"
#include "dialog/vtipsdialog.h"
#include "vcart.h"
#include "vhistorylist.h"

extern VConfigManager *g_config;
extern VNote *g_vnote;
extern VMainWindow *g_mainWin;

VFileList::VFileList(QWidget *parent)
    : QWidget(parent),
      VNavigationMode(),
      m_openWithMenu(NULL),
      m_itemClicked(NULL),
      m_fileToCloseInSingleClick(NULL)
{
    setupUI();
    initShortcuts();

    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(QApplication::doubleClickInterval());
    // When timer timeouts, we need to close the previous tab to simulate the
    // effect as opening file in current tab.
    connect(m_clickTimer, &QTimer::timeout,
            this, [this]() {
                m_itemClicked = NULL;
                VFile *file = m_fileToCloseInSingleClick;
                m_fileToCloseInSingleClick = NULL;

                if (file) {
                    editArea->closeFile(file, false);
                    fileList->setFocus();
                }
            });

    updateNumberLabel();
}

void VFileList::setupUI()
{
    QLabel *titleLabel = new QLabel(tr("Notes"), this);
    titleLabel->setProperty("TitleLabel", true);

    QPushButton *viewBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/view.svg"),
                                           "",
                                           this);
    viewBtn->setToolTip(tr("View"));
    viewBtn->setProperty("CornerBtn", true);
    viewBtn->setFocusPolicy(Qt::NoFocus);

    QMenu *viewMenu = new QMenu(this);
    connect(viewMenu, &QMenu::aboutToShow,
            this, [this, viewMenu]() {
                updateViewMenu(viewMenu);
            });
    viewBtn->setMenu(viewMenu);

    m_splitBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/split_window.svg"),
                                 "",
                                 this);
    m_splitBtn->setToolTip(tr("Split"));
    m_splitBtn->setCheckable(true);
    m_splitBtn->setProperty("CornerBtn", true);
    m_splitBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_splitBtn, &QPushButton::clicked,
            this, [this](bool p_checked) {
                emit requestSplitOut(p_checked);
            });

    m_numLabel = new QLabel(this);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(viewBtn);
    titleLayout->addWidget(m_splitBtn);
    titleLayout->addStretch();
    titleLayout->addWidget(m_numLabel);

    titleLayout->setContentsMargins(0, 0, 0, 0);

    fileList = new VFileListWidget(this);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    fileList->setObjectName("FileList");
    fileList->setAttribute(Qt::WA_MacShowFocusRect, false);
    fileList->setMimeDataGetter([this](const QString &p_format,
                                       const QList<QListWidgetItem *> &p_items) {
                return getMimeData(p_format, p_items);
            });

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(titleLayout);
    mainLayout->addWidget(fileList);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    connect(fileList, &QListWidget::customContextMenuRequested,
            this, &VFileList::contextMenuRequested);
    connect(fileList, &QListWidget::itemClicked,
            this, &VFileList::handleItemClicked);
    connect(fileList, &QListWidget::itemEntered,
            this, &VFileList::showStatusTipAboutItem);
    connect(fileList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *p_cur, QListWidgetItem *p_pre) {
                Q_UNUSED(p_pre);
                if (p_cur) {
                    showStatusTipAboutItem(p_cur);
                }
            });

    setLayout(mainLayout);
}

void VFileList::initShortcuts()
{
    QShortcut *infoShortcut = new QShortcut(QKeySequence(Shortcut::c_info), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, [this](){
                fileInfo();
            });

    QShortcut *copyShortcut = new QShortcut(QKeySequence(Shortcut::c_copy), this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated,
            this, [this](){
                copySelectedFiles();
            });

    QShortcut *cutShortcut = new QShortcut(QKeySequence(Shortcut::c_cut), this);
    cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cutShortcut, &QShortcut::activated,
            this, [this](){
                cutSelectedFiles();
            });

    QShortcut *pasteShortcut = new QShortcut(QKeySequence(Shortcut::c_paste), this);
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated,
            this, [this](){
                pasteFilesFromClipboard();
            });

    QKeySequence seq(g_config->getShortcutKeySequence("OpenViaDefaultProgram"));
    if (!seq.isEmpty()) {
        QShortcut *defaultProgramShortcut = new QShortcut(seq, this);
        defaultProgramShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(defaultProgramShortcut, &QShortcut::activated,
                this, &VFileList::openCurrentItemViaDefaultProgram);
    }
}

void VFileList::setDirectory(VDirectory *p_directory)
{
    // QPointer will be set to NULL automatically once the directory was deleted.
    // If the last directory is deleted, m_directory and p_directory will both
    // be NULL.
    if (m_directory == p_directory) {
        if (!m_directory) {
            fileList->clearAll();
            updateNumberLabel();
        }

        return;
    }

    m_directory = p_directory;
    if (!m_directory) {
        fileList->clearAll();
        updateNumberLabel();
        return;
    }

    updateFileList();
}

void VFileList::updateFileList()
{
    fileList->clearAll();
    if (!m_directory->open()) {
        return;
    }

    QVector<VNoteFile *> files = m_directory->getFiles();
    sortFiles(files, (ViewOrder)g_config->getNoteListViewOrder());

    for (int i = 0; i < files.size(); ++i) {
        VNoteFile *file = files[i];
        insertFileListItem(file);
    }

    updateNumberLabel();
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
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.size() == 1) {
        QUrl url = QUrl::fromLocalFile(getVFile(items[0])->fetchBasePath());
        QDesktopServices::openUrl(url);
    }
}

void VFileList::addFileToCart() const
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    VCart *cart = g_mainWin->getCart();

    for (int i = 0; i < items.size(); ++i) {
        cart->addFile(getVFile(items[i])->fetchPath());
    }

    g_mainWin->showStatusMessage(tr("%1 %2 added to Cart")
                                   .arg(items.size())
                                   .arg(items.size() > 1 ? tr("notes") : tr("note")));
}

void VFileList::pinFileToHistory() const
{
    QList<QListWidgetItem *> items = fileList->selectedItems();

    QStringList files;
    for (int i = 0; i < items.size(); ++i) {
        files << getVFile(items[i])->fetchPath();
    }

    g_mainWin->getHistoryList()->pinFiles(files);

    g_mainWin->showStatusMessage(tr("%1 %2 pinned to History")
                                   .arg(items.size())
                                   .arg(items.size() > 1 ? tr("notes") : tr("note")));
}

void VFileList::setFileQuickAccess() const
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.size() == 1) {
        QString fp(getVFile(items[0])->fetchPath());

        g_config->setQuickAccess(fp);
        g_mainWin->showStatusMessage(tr("Quick access: %1").arg(fp));
    }
}

void VFileList::fileInfo(VNoteFile *p_file)
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

        emit fileUpdated(p_file, UpdateAction::InfoChanged);
    }
}

void VFileList::fillItem(QListWidgetItem *p_item, const VNoteFile *p_file)
{
    qulonglong ptr = (qulonglong)p_file;
    p_item->setData(Qt::UserRole, ptr);
    p_item->setText(p_file->getName());

    QString createdTime = VUtils::displayDateTime(p_file->getCreatedTimeUtc().toLocalTime());
    QString modifiedTime = VUtils::displayDateTime(p_file->getModifiedTimeUtc().toLocalTime());
    QString tooltip = tr("%1\n\nCreated Time: %2\nModified Time: %3")
                        .arg(p_file->getName())
                        .arg(createdTime)
                        .arg(modifiedTime);
    p_item->setToolTip(tooltip);
    V_ASSERT(sizeof(p_file) <= sizeof(ptr));
}

QListWidgetItem* VFileList::insertFileListItem(VNoteFile *file, bool atFront)
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
    return item;
}

void VFileList::removeFileListItem(VNoteFile *p_file)
{
    if (!p_file) {
        return;
    }

    QListWidgetItem *item = findItem(p_file);
    if (!item) {
        return;
    }

    int row = fileList->row(item);
    Q_ASSERT(row >= 0);

    fileList->takeItem(row);
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
    defaultName = VUtils::getFileNameWithSequence(m_directory->fetchPath(),
                                                  defaultName,
                                                  true);
    VNewFileDialog dialog(tr("Create Note"), info, defaultName, m_directory, this);
    if (dialog.exec() == QDialog::Accepted) {
        VNoteFile *file = m_directory->createFile(dialog.getNameInput(),
                                                  g_config->getInsertNewNoteInFront());
        if (!file) {
            VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                tr("Fail to create note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle).arg(dialog.getNameInput()), "",
                                QMessageBox::Ok, QMessageBox::Ok, this);
            return;
        }

        // Whether need to move the cursor to the end.
        bool moveCursorEnd = false;
        // Content needed to insert into the new file, title/template.
        QString insertContent;
        if (dialog.getInsertTitleInput() && file->getDocType() == DocType::Markdown) {
            // Insert title.
            insertContent = QString("# %1\n").arg(QFileInfo(file->getName()).completeBaseName());
        }

        if (dialog.isTemplateUsed()) {
            Q_ASSERT(insertContent.isEmpty());
            insertContent = dialog.getTemplate();
        }

        if (!insertContent.isEmpty()) {
            if (!file->open()) {
                qWarning() << "fail to open newly-created note" << file->getName();
            } else {
                Q_ASSERT(file->getContent().isEmpty());
                file->setContent(insertContent);
                if (!file->save()) {
                    qWarning() << "fail to write to newly-created note" << file->getName();
                } else {
                    if (dialog.getInsertTitleInput()) {
                        moveCursorEnd = true;
                    }
                }

                file->close();
            }
        }

        updateFileList();

        locateFile(file);

        // Open it in edit mode
        emit fileCreated(file, OpenFileMode::Edit, true);

        // Move cursor down if content has been inserted.
        if (moveCursorEnd) {
            VMdTab *tab = dynamic_cast<VMdTab *>(editArea->getCurrentTab());
            if (tab) {
                // It will init markdown editor (within 50 ms)
                // See VMdTab::VMdTab --> QTimer::singleShot(50, ...
                VMdEditor *edit = tab->getEditor();
                if (edit && edit->getFile() == file) {
                    QTextCursor cursor = edit->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    edit->setTextCursor(cursor);
                }
            }
        }
    }
}

void VFileList::deleteSelectedFiles()
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    Q_ASSERT(!items.isEmpty());

    QVector<VNoteFile *> files;
    for (auto const & item : items) {
        files.push_back(getVFile(item));
    }

    deleteFiles(files);
}

// @p_file may or may not be listed in VFileList
void VFileList::deleteFile(VNoteFile *p_file)
{
    if (!p_file) {
        return;
    }

    QVector<VNoteFile *> files(1, p_file);
    deleteFiles(files);
}

void VFileList::deleteFiles(const QVector<VNoteFile *> &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    QVector<ConfirmItemInfo> items;
    for (auto const & file : p_files) {
        items.push_back(ConfirmItemInfo(file->getName(),
                                        file->fetchPath(),
                                        file->fetchPath(),
                                        (void *)file));
    }

    QString text = tr("Are you sure to delete these notes?");

    QString info = tr("<span style=\"%1\">WARNING</span>: "
                      "VNote will delete notes as well as all "
                      "their images and attachments managed by VNote. "
                      "Deleted files could be found in the recycle "
                      "bin of these notes.<br>"
                      "Click \"Cancel\" to leave them untouched.<br>"
                      "The operation is IRREVERSIBLE!")
                     .arg(g_config->c_warningTextStyle);

    VConfirmDeletionDialog dialog(tr("Confirm Deleting Notes"),
                                  text,
                                  info,
                                  items,
                                  false,
                                  false,
                                  false,
                                  this);
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();
        QVector<VNoteFile *> files;
        for (auto const & item : items) {
            files.push_back((VNoteFile *)item.m_data);
        }

        int nrDeleted = 0;
        for (auto file : files) {
            editArea->closeFile(file, true);

            // Remove the item before deleting it totally, or file will be invalid.
            removeFileListItem(file);

            QString errMsg;
            QString fileName = file->getName();
            QString filePath = file->fetchPath();
            if (!VNoteFile::deleteFile(file, &errMsg)) {
                VUtils::showMessage(QMessageBox::Warning,
                                    tr("Warning"),
                                    tr("Fail to delete note <span style=\"%1\">%2</span>.<br>"
                                       "Please check <span style=\"%1\">%3</span> and manually delete it.")
                                      .arg(g_config->c_dataTextStyle)
                                      .arg(fileName)
                                      .arg(filePath),
                                    errMsg,
                                    QMessageBox::Ok,
                                    QMessageBox::Ok,
                                    this);
            } else {
                Q_ASSERT(errMsg.isEmpty());
                ++nrDeleted;
            }
        }

        if (nrDeleted > 0) {
            g_mainWin->showStatusMessage(tr("%1 %2 deleted")
                                           .arg(nrDeleted)
                                           .arg(nrDeleted > 1 ? tr("notes") : tr("note")));
            updateNumberLabel();
        }
    }
}

void VFileList::contextMenuRequested(QPoint pos)
{
    if (!m_directory) {
        return;
    }

    QListWidgetItem *item = fileList->itemAt(pos);
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    int selectedSize = fileList->selectedItems().size();

    if (item && selectedSize == 1) {
        VNoteFile *file = getVFile(item);
        if (file) {
            if (file->getDocType() == DocType::Markdown) {
                QAction *openInReadAct = new QAction(VIconUtils::menuIcon(":/resources/icons/reading.svg"),
                                                     tr("&Open In Read Mode"),
                                                     &menu);
                openInReadAct->setToolTip(tr("Open current note in read mode"));
                connect(openInReadAct, &QAction::triggered,
                        this, [this]() {
                            QListWidgetItem *item = fileList->currentItem();
                            if (item) {
                                emit fileClicked(getVFile(item), OpenFileMode::Read, true);
                            }
                        });
                menu.addAction(openInReadAct);

                QAction *openInEditAct = new QAction(VIconUtils::menuIcon(":/resources/icons/editing.svg"),
                                                     tr("Open In &Edit Mode"),
                                                     &menu);
                openInEditAct->setToolTip(tr("Open current note in edit mode"));
                connect(openInEditAct, &QAction::triggered,
                        this, [this]() {
                            QListWidgetItem *item = fileList->currentItem();
                            if (item) {
                                emit fileClicked(getVFile(item), OpenFileMode::Edit, true);
                            }
                        });
                menu.addAction(openInEditAct);
            }

            menu.addMenu(getOpenWithMenu());

            menu.addSeparator();
        }
    }

    QAction *newFileAct = new QAction(VIconUtils::menuIcon(":/resources/icons/create_note.svg"),
                                      tr("&New Note"),
                                      &menu);
    VUtils::fixTextWithShortcut(newFileAct, "NewNote");
    newFileAct->setToolTip(tr("Create a note in current folder"));
    connect(newFileAct, SIGNAL(triggered(bool)),
            this, SLOT(newFile()));
    menu.addAction(newFileAct);

    if (fileList->count() > 1) {
        QAction *sortAct = new QAction(VIconUtils::menuIcon(":/resources/icons/sort.svg"),
                                       tr("&Sort"),
                                       &menu);
        sortAct->setToolTip(tr("Sort notes in this folder manually"));
        connect(sortAct, &QAction::triggered,
                this, &VFileList::sortItems);
        menu.addAction(sortAct);
    }

    if (item) {
        menu.addSeparator();

        QAction *deleteFileAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/delete_note.svg"),
                                             tr("&Delete"),
                                             &menu);
        deleteFileAct->setToolTip(tr("Delete selected note"));
        connect(deleteFileAct, SIGNAL(triggered(bool)),
                this, SLOT(deleteSelectedFiles()));
        menu.addAction(deleteFileAct);

        QAction *copyAct = new QAction(VIconUtils::menuIcon(":/resources/icons/copy.svg"),
                                       tr("&Copy\t%1").arg(VUtils::getShortcutText(Shortcut::c_copy)),
                                       &menu);
        copyAct->setToolTip(tr("Copy selected notes"));
        connect(copyAct, &QAction::triggered,
                this, &VFileList::copySelectedFiles);
        menu.addAction(copyAct);

        QAction *cutAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cut.svg"),
                                      tr("C&ut\t%1").arg(VUtils::getShortcutText(Shortcut::c_cut)),
                                      &menu);
        cutAct->setToolTip(tr("Cut selected notes"));
        connect(cutAct, &QAction::triggered,
                this, &VFileList::cutSelectedFiles);
        menu.addAction(cutAct);
    }

    if (pasteAvailable()) {
        if (!item) {
            menu.addSeparator();
        }

        QAction *pasteAct = new QAction(VIconUtils::menuIcon(":/resources/icons/paste.svg"),
                                        tr("&Paste\t%1").arg(VUtils::getShortcutText(Shortcut::c_paste)),
                                        &menu);
        pasteAct->setToolTip(tr("Paste notes in current folder"));
        connect(pasteAct, &QAction::triggered,
                this, &VFileList::pasteFilesFromClipboard);
        menu.addAction(pasteAct);
    }

    if (item) {
        menu.addSeparator();
        if (selectedSize == 1) {
            QAction *openLocationAct = new QAction(VIconUtils::menuIcon(":/resources/icons/open_location.svg"),
                                                   tr("Open Note &Location"),
                                                   &menu);
            openLocationAct->setToolTip(tr("Explore the folder containing this note in operating system"));
            connect(openLocationAct, &QAction::triggered,
                    this, &VFileList::openFileLocation);
            menu.addAction(openLocationAct);

            QAction *copyPathAct = new QAction(tr("Copy File &Path"), &menu);
            connect(copyPathAct, &QAction::triggered,
                    this, [this]() {
                        QList<QListWidgetItem *> items = fileList->selectedItems();
                        if (items.size() == 1) {
                            QString filePath = getVFile(items[0])->fetchPath();
                            QClipboard *clipboard = QApplication::clipboard();
                            clipboard->setText(filePath);
                            g_mainWin->showStatusMessage(tr("File path copied %1").arg(filePath));
                        }
                    });
            menu.addAction(copyPathAct);
        }

        QAction *addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                            tr("Add To Cart"),
                                            &menu);
        addToCartAct->setToolTip(tr("Add selected notes to Cart for further processing"));
        connect(addToCartAct, &QAction::triggered,
                this, &VFileList::addFileToCart);
        menu.addAction(addToCartAct);

        QAction *pinToHistoryAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                               tr("Pin To History"),
                                               &menu);
        pinToHistoryAct->setToolTip(tr("Pin selected notes to History"));
        connect(pinToHistoryAct, &QAction::triggered,
                this, &VFileList::pinFileToHistory);
        menu.addAction(pinToHistoryAct);

        QAction *quickAccessAct = new QAction(VIconUtils::menuIcon(":/resources/icons/quick_access.svg"),
                                              tr("Set As Quick Access"),
                                              &menu);
        quickAccessAct->setToolTip(tr("Set current note as quick access"));
        connect(quickAccessAct, &QAction::triggered,
                this, &VFileList::setFileQuickAccess);
        menu.addAction(quickAccessAct);

        if (selectedSize == 1) {
            QAction *fileInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/note_info.svg"),
                                               tr("&Info (Rename)\t%1").arg(VUtils::getShortcutText(Shortcut::c_info)),
                                               &menu);
            fileInfoAct->setToolTip(tr("View and edit current note's information"));
            connect(fileInfoAct, SIGNAL(triggered(bool)),
                    this, SLOT(fileInfo()));
            menu.addAction(fileInfoAct);
        }
    }

    menu.exec(fileList->mapToGlobal(pos));
}

QListWidgetItem* VFileList::findItem(const VNoteFile *p_file)
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

void VFileList::handleItemClicked(QListWidgetItem *p_item)
{
    Q_ASSERT(p_item);

    Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    if (modifiers != Qt::NoModifier) {
        return;
    }

    m_clickTimer->stop();
    if (m_itemClicked) {
        // Timer will not trigger.
        if (m_itemClicked == p_item) {
            // Double clicked.
            m_itemClicked = NULL;
            m_fileToCloseInSingleClick = NULL;
            return;
        } else {
            // Handle previous clicked item as single click.
            m_itemClicked = NULL;
            if (m_fileToCloseInSingleClick) {
                editArea->closeFile(m_fileToCloseInSingleClick, false);
                m_fileToCloseInSingleClick = NULL;
            }
        }
    }

    // Pending @p_item.
    bool singleClickClose = g_config->getSingleClickClosePreviousTab();
    if (singleClickClose) {
        VFile *file = getVFile(p_item);
        Q_ASSERT(file);
        if (editArea->isFileOpened(file)) {
            // File already opened.
            activateItem(p_item, true);
            return;
        }

        // EditArea will open Unknown file using system's default program, in which
        // case we should now close current file even after single click.
        if (file->getDocType() == DocType::Unknown) {
            m_fileToCloseInSingleClick = NULL;
        } else {
            // Get current tab which will be closed if click timer timeouts.
            VEditTab *tab = editArea->getCurrentTab();
            if (tab) {
                m_fileToCloseInSingleClick = tab->getFile();
            } else {
                m_fileToCloseInSingleClick = NULL;
            }
        }
    }

    // Activate it.
    activateItem(p_item, true);

    if (singleClickClose) {
        m_itemClicked = p_item;
        m_clickTimer->start();
    }
}

void VFileList::showStatusTipAboutItem(QListWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    const VNoteFile *file = getVFile(p_item);
    const QStringList &tags = file->getTags();
    QString tag;
    for (int i = 0; i < tags.size(); ++i) {
        if (i == 0) {
            tag = "\t[" + tags[i] + "]";
        } else {
            tag += " [" + tags[i] + "]";
        }
    }

    QString tip(file->getName() + tag);
    g_mainWin->showStatusMessage(tip);
}

void VFileList::activateItem(QListWidgetItem *p_item, bool p_restoreFocus)
{
    if (!p_item) {
        emit fileClicked(NULL);
        return;
    }

    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    fileList->update();
    emit fileClicked(getVFile(p_item), g_config->getNoteOpenMode());

    if (p_restoreFocus) {
        fileList->setFocus();
    }
}

bool VFileList::importFiles(const QStringList &p_files, QString *p_errMsg)
{
    if (p_files.isEmpty()) {
        return false;
    }

    bool ret = true;
    Q_ASSERT(m_directory && m_directory->isOpened());
    QString dirPath = m_directory->fetchPath();
    QDir dir(dirPath);

    QVector<VNoteFile *> importedFiles;
    for (int i = 0; i < p_files.size(); ++i) {
        const QString &file = p_files[i];

        QFileInfo fi(file);
        if (!fi.exists() || !fi.isFile()) {
            VUtils::addErrMsg(p_errMsg, tr("Skip importing non-exist file %1.")
                                          .arg(file));
            ret = false;
            continue;
        }

        QString name = VUtils::fileNameFromPath(file);
        Q_ASSERT(!name.isEmpty());

        bool copyNeeded = true;
        if (VUtils::equalPath(dirPath, fi.absolutePath())) {
            // Check if it is already a note.
            if (m_directory->findFile(name, false)) {
                VUtils::addErrMsg(p_errMsg, tr("Skip importing file %1. "
                                               "A note with the same name (case-insensitive) "
                                               "in the same directory already exists.")
                                              .arg(file));
                ret = false;
                continue;
            }

            qDebug() << "skip cpoy file" << file << "locates in" << dirPath;
            copyNeeded = false;
        }

        QString targetFilePath;
        if (copyNeeded) {
            name = VUtils::getFileNameWithSequence(dirPath, name, true);
            targetFilePath = dir.filePath(name);
            bool ret = VUtils::copyFile(file, targetFilePath, false);
            if (!ret) {
                VUtils::addErrMsg(p_errMsg, tr("Fail to copy file %1 as %2.")
                                              .arg(file)
                                              .arg(targetFilePath));
                ret = false;
                continue;
            }
        } else {
            targetFilePath = file;
        }

        VNoteFile *destFile = m_directory->addFile(name, -1);
        if (destFile) {
            importedFiles.append(destFile);
            qDebug() << "imported" << file << "as" << targetFilePath;
        } else {
            VUtils::addErrMsg(p_errMsg, tr("Fail to add the note %1 to target folder's configuration.")
                                          .arg(file));
            ret = false;
            continue;
        }
    }

    qDebug() << "imported" << importedFiles.size() << "files";

    updateFileList();

    selectFiles(importedFiles);

    return ret;
}

void VFileList::copySelectedFiles(bool p_isCut)
{
    QList<QListWidgetItem *> items = fileList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    QJsonArray files;
    for (int i = 0; i < items.size(); ++i) {
        VNoteFile *file = getVFile(items[i]);
        files.append(file->fetchPath());
    }

    QJsonObject clip;
    clip[ClipboardConfig::c_magic] = getNewMagic();
    clip[ClipboardConfig::c_type] = (int)ClipboardOpType::CopyFile;
    clip[ClipboardConfig::c_isCut] = p_isCut;
    clip[ClipboardConfig::c_files] = files;

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(QJsonDocument(clip).toJson(QJsonDocument::Compact));

    qDebug() << "copied files info" << clipboard->text();

    int cnt = files.size();
    g_mainWin->showStatusMessage(tr("%1 %2 %3")
                                   .arg(cnt)
                                   .arg(cnt > 1 ? tr("notes") : tr("note"))
                                   .arg(p_isCut ? tr("cut") : tr("copied")));
}

void VFileList::cutSelectedFiles()
{
    copySelectedFiles(true);
}

void VFileList::pasteFilesFromClipboard()
{
    if (!pasteAvailable()) {
        return;
    }

    QJsonObject obj = VUtils::clipboardToJson();
    QJsonArray files = obj[ClipboardConfig::c_files].toArray();
    bool isCut = obj[ClipboardConfig::c_isCut].toBool();
    QVector<QString> filesToPaste(files.size());
    for (int i = 0; i < files.size(); ++i) {
        filesToPaste[i] = files[i].toString();
    }

    pasteFiles(m_directory, filesToPaste, isCut);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->clear();
}

void VFileList::pasteFiles(VDirectory *p_destDir,
                           const QVector<QString> &p_files,
                           bool p_isCut)
{
    if (!p_destDir || p_files.isEmpty()) {
        return;
    }

    int nrPasted = 0;
    for (int i = 0; i < p_files.size(); ++i) {
        VNoteFile *file = g_vnote->getInternalFile(p_files[i]);
        if (!file) {
            qWarning() << "Copied file is not an internal note" << p_files[i];
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to paste note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(p_files[i]),
                                tr("VNote could not find this note in any notebook."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);

            continue;
        }

        QString fileName = file->getName();
        if (file->getDirectory() == p_destDir) {
            if (p_isCut) {
                qDebug() << "skip one note to cut and paste in the same folder" << fileName;
                continue;
            }

            // Copy and paste in the same folder.
            // We do not allow this if the note contains local images.
            if (file->getDocType() == DocType::Markdown) {
                QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(file,
                                                                                ImageLink::LocalRelativeInternal);
                if (!images.isEmpty()) {
                    qDebug() << "skip one note with internal images to copy and paste in the same folder"
                             << fileName;
                    VUtils::showMessage(QMessageBox::Warning,
                                        tr("Warning"),
                                        tr("Fail to copy note <span style=\"%1\">%2</span>.")
                                          .arg(g_config->c_dataTextStyle)
                                          .arg(p_files[i]),
                                        tr("VNote does not allow copy and paste notes with internal images "
                                           "in the same folder."),
                                        QMessageBox::Ok,
                                        QMessageBox::Ok,
                                        this);
                    continue;
                }
            }

            // Rename it to xxx_copy.md.
            fileName = VUtils::generateCopiedFileName(file->fetchBasePath(),
                                                      fileName,
                                                      true);
        } else {
            // Rename it to xxx_copy.md if needed.
            fileName = VUtils::generateCopiedFileName(p_destDir->fetchPath(),
                                                      fileName,
                                                      true);
        }

        QString msg;
        VNoteFile *destFile = NULL;
        bool ret = VNoteFile::copyFile(p_destDir,
                                       fileName,
                                       file,
                                       p_isCut,
                                       g_config->getInsertNewNoteInFront() ? 0 : -1,
                                       &destFile,
                                       &msg);
        if (!ret) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to copy note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(p_files[i]),
                                msg,
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }

        if (destFile) {
            ++nrPasted;
            emit fileUpdated(destFile, p_isCut ? UpdateAction::Moved : UpdateAction::InfoChanged);
        }
    }

    qDebug() << "pasted" << nrPasted << "files";
    if (nrPasted > 0) {
        g_mainWin->showStatusMessage(tr("%1 %2 pasted")
                                       .arg(nrPasted)
                                       .arg(nrPasted > 1 ? tr("notes") : tr("note")));
    }

    updateFileList();
    getNewMagic();
}

void VFileList::keyPressEvent(QKeyEvent *p_event)
{
    switch (p_event->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    {
        QListWidgetItem *item = fileList->currentItem();
        if (item) {
            VFile *fileToClose = NULL;
            VFile *file = getVFile(item);
            Q_ASSERT(file);
            if (p_event->modifiers() == Qt::NoModifier
                && g_config->getSingleClickClosePreviousTab()
                && file->getDocType() != DocType::Unknown) {
                if (!editArea->isFileOpened(file)) {
                    VEditTab *tab = editArea->getCurrentTab();
                    if (tab) {
                        fileToClose = tab->getFile();
                    }
                }

            }

            activateItem(item, false);
            if (fileToClose) {
                editArea->closeFile(fileToClose, false);
            }
        }

        break;
    }

    default:
        break;
    }

    QWidget::keyPressEvent(p_event);
}

void VFileList::focusInEvent(QFocusEvent * /* p_event */)
{
    fileList->setFocus();
}

bool VFileList::locateFile(const VNoteFile *p_file)
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

void VFileList::showNavigation()
{
    VNavigationMode::showNavigation(fileList);
}

bool VFileList::handleKeyNavigation(int p_key, bool &p_succeed)
{
    return VNavigationMode::handleKeyNavigation(fileList, p_key, p_succeed);
}

int VFileList::getNewMagic()
{
    m_magicForClipboard = (int)QDateTime::currentDateTime().toTime_t();
    m_magicForClipboard |= qrand();

    return m_magicForClipboard;
}

bool VFileList::checkMagic(int p_magic) const
{
    return m_magicForClipboard == p_magic;
}

bool VFileList::pasteAvailable() const
{
    QJsonObject obj = VUtils::clipboardToJson();
    if (obj.isEmpty()) {
        return false;
    }

    if (!obj.contains(ClipboardConfig::c_type)) {
        return false;
    }

    ClipboardOpType type = (ClipboardOpType)obj[ClipboardConfig::c_type].toInt();
    if (type != ClipboardOpType::CopyFile) {
        return false;
    }

    if (!obj.contains(ClipboardConfig::c_magic)
        || !obj.contains(ClipboardConfig::c_isCut)
        || !obj.contains(ClipboardConfig::c_files)) {
        return false;
    }

    int magic = obj[ClipboardConfig::c_magic].toInt();
    if (!checkMagic(magic)) {
        return false;
    }

    QJsonArray files = obj[ClipboardConfig::c_files].toArray();
    return !files.isEmpty();
}

void VFileList::sortItems()
{
    const QVector<VNoteFile *> &files = m_directory->getFiles();
    if (files.size() < 2) {
        return;
    }

    VSortDialog dialog(tr("Sort Notes"),
                       tr("Sort notes in folder <span style=\"%1\">%2</span> "
                          "in the configuration file.")
                         .arg(g_config->c_dataTextStyle)
                         .arg(m_directory->getName()),
                       this);
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(3);
    QStringList headers;
    headers << tr("Name") << tr("Created Time") << tr("Modified Time");
    tree->setHeaderLabels(headers);

    for (int i = 0; i < files.size(); ++i) {
        const VNoteFile *file = files[i];
        QString createdTime = VUtils::displayDateTime(file->getCreatedTimeUtc().toLocalTime(), true);
        QString modifiedTime = VUtils::displayDateTime(file->getModifiedTimeUtc().toLocalTime(), true);
        QStringList cols;
        cols << file->getName() << createdTime << modifiedTime;
        QTreeWidgetItem *item = new QTreeWidgetItem(tree, cols);

        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        QVector<QVariant> data = dialog.getSortedData();
        Q_ASSERT(data.size() == files.size());
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }

        qDebug() << "sort files" << sortedIdx;
        if (!m_directory->sortFiles(sortedIdx)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to sort notes in folder <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_directory->getName()),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }

        updateFileList();
    }
}

QMenu *VFileList::getOpenWithMenu()
{
    if (m_openWithMenu) {
        return m_openWithMenu;
    }

    m_openWithMenu = new QMenu(tr("Open With"), this);
    m_openWithMenu->setToolTipsVisible(true);

    auto programs = g_config->getExternalEditors();
    for (auto const & pa : programs) {
        QKeySequence seq = QKeySequence(pa.m_shortcut);
        QString name = pa.m_name;
        if (!seq.isEmpty()) {
            name = QString("%1\t%2").arg(pa.m_name)
                                    .arg(VUtils::getShortcutText(pa.m_shortcut));
        }

        QAction *act = new QAction(name, this);
        act->setToolTip(tr("Open current note with %1").arg(pa.m_name));
        act->setStatusTip(pa.m_cmd);
        act->setData(pa.m_cmd);

        if (!seq.isEmpty()) {
            QShortcut *shortcut = new QShortcut(seq, this);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut, &QShortcut::activated,
                    this, [act](){
                        act->trigger();
                    });
        }

        connect(act, &QAction::triggered,
                this, &VFileList::handleOpenWithActionTriggered);

        m_openWithMenu->addAction(act);
    }

    QKeySequence seq(g_config->getShortcutKeySequence("OpenViaDefaultProgram"));
    QString name = tr("System's Default Program");
    if (!seq.isEmpty()) {
        name = QString("%1\t%2").arg(name)
                                .arg(VUtils::getShortcutText(g_config->getShortcutKeySequence("OpenViaDefaultProgram")));
    }
    QAction *defaultAct = new QAction(name, this);
    defaultAct->setToolTip(tr("Open current note with system's default program"));
    connect(defaultAct, &QAction::triggered,
            this, &VFileList::openCurrentItemViaDefaultProgram);

    m_openWithMenu->addAction(defaultAct);

    QAction *addAct = new QAction(VIconUtils::menuIcon(":/resources/icons/add_program.svg"),
                                  tr("Add External Program"),
                                  this);
    addAct->setToolTip(tr("Add external program"));
    connect(addAct, &QAction::triggered,
            this, [this]() {
                VTipsDialog dialog(VUtils::getDocFile("tips_external_program.md"),
                                   tr("Add External Program"),
                                   []() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
                                       // On macOS, it seems that we could not open that ini file directly.
                                       QUrl url = QUrl::fromLocalFile(g_config->getConfigFolder());
#else
                                       QUrl url = QUrl::fromLocalFile(g_config->getConfigFilePath());
#endif
                                       QDesktopServices::openUrl(url);
                                   },
                                   this);
                dialog.exec();
            });

    m_openWithMenu->addAction(addAct);

    return m_openWithMenu;
}

void VFileList::handleOpenWithActionTriggered()
{
    QAction *act = static_cast<QAction *>(sender());
    QString cmd = act->data().toString();

    QListWidgetItem *item = fileList->currentItem();
    if (item) {
        VNoteFile *file = getVFile(item);
        if (file
            && (!g_config->getCloseBeforeExternalEditor()
                || !editArea->isFileOpened(file)
                || editArea->closeFile(file, false))) {
            cmd.replace("%0", file->fetchPath());
            QProcess *process = new QProcess(this);
            connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    process, &QProcess::deleteLater);
            process->start(cmd);
        }
    }
}

void VFileList::updateNumberLabel() const
{
    int cnt = fileList->count();
    m_numLabel->setText(tr("%1 %2").arg(cnt)
                                   .arg(cnt > 1 ? tr("Items") : tr("Item")));
}

void VFileList::updateViewMenu(QMenu *p_menu)
{
    if (p_menu->isEmpty()) {
        QActionGroup *ag = new QActionGroup(p_menu);

        QAction *act = new QAction(tr("View By Configuration File"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::Config);
        act->setChecked(true);
        p_menu->addAction(act);

        act = new QAction(tr("View By Name"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::Name);
        p_menu->addAction(act);

        act = new QAction(tr("View By Name (Reverse)"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::NameReverse);
        p_menu->addAction(act);

        act = new QAction(tr("View By Created Time"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::CreatedTime);
        p_menu->addAction(act);

        act = new QAction(tr("View By Created Time (Reverse)"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::CreatedTimeReverse);
        p_menu->addAction(act);

        act = new QAction(tr("View By Modified Time"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::ModifiedTime);
        p_menu->addAction(act);

        act = new QAction(tr("View By Modified Time (Reverse)"), ag);
        act->setCheckable(true);
        act->setData(ViewOrder::ModifiedTimeReverse);
        p_menu->addAction(act);

        int config = g_config->getNoteListViewOrder();
        for (auto act : ag->actions()) {
            if (act->data().toInt() == config) {
                act->setChecked(true);
            }
        }

        connect(ag, &QActionGroup::triggered,
                this, [this](QAction *p_action) {
                    int order = p_action->data().toInt();
                    g_config->setNoteListViewOrder(order);

                    sortFileList((ViewOrder)order);
                });
    }
}

void VFileList::sortFileList(ViewOrder p_order)
{
    if (!m_directory) {
        return;
    }

    QVector<VNoteFile *> files = m_directory->getFiles();
    if (files.isEmpty()) {
        return;
    }

    sortFiles(files, p_order);

    int cnt = files.size();
    Q_ASSERT(cnt == fileList->count());
    for (int i = 0; i < cnt; ++i) {
        int row = -1;
        for (int j = i; j < cnt; ++j) {
            QListWidgetItem *item = fileList->item(j);
            if (files[i] == getVFile(item)) {
                row = j;
                break;
            }
        }

        Q_ASSERT(row > -1);
        QListWidgetItem *item = fileList->takeItem(row);
        fileList->insertItem(i, item);
    }
}

void VFileList::sortFiles(QVector<VNoteFile *> &p_files, ViewOrder p_order)
{
    bool reverse = false;

    switch (p_order) {
    case ViewOrder::Config:
        break;

    case ViewOrder::NameReverse:
        reverse = true;
        V_FALLTHROUGH;
    case ViewOrder::Name:
        std::sort(p_files.begin(), p_files.end(), [reverse](const VNoteFile *p_a, const VNoteFile *p_b) {
            if (reverse) {
                return p_b->getName() < p_a->getName();
            } else {
                return p_a->getName() < p_b->getName();
            }
        });
        break;

    case ViewOrder::CreatedTimeReverse:
        reverse = true;
        V_FALLTHROUGH;
    case ViewOrder::CreatedTime:
        std::sort(p_files.begin(), p_files.end(), [reverse](const VNoteFile *p_a, const VNoteFile *p_b) {
            if (reverse) {
                return p_b->getCreatedTimeUtc() < p_a->getCreatedTimeUtc();
            } else {
                return p_a->getCreatedTimeUtc() < p_b->getCreatedTimeUtc();
            }
        });
        break;

    case ViewOrder::ModifiedTimeReverse:
        reverse = true;
        V_FALLTHROUGH;
    case ViewOrder::ModifiedTime:
        std::sort(p_files.begin(), p_files.end(), [reverse](const VNoteFile *p_a, const VNoteFile *p_b) {
            if (reverse) {
                return p_b->getModifiedTimeUtc() < p_a->getModifiedTimeUtc();
            } else {
                return p_a->getModifiedTimeUtc() < p_b->getModifiedTimeUtc();
            }
        });
        break;

    default:
        break;
    }
}

void VFileList::selectFiles(const QVector<VNoteFile *> &p_files)
{
    bool first = true;
    for (auto const & file : p_files) {
        QListWidgetItem *item = findItem(file);
        if (item) {
            if (first) {
                first = false;
                fileList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            } else {
                item->setSelected(true);
            }
        }
    }
}

QByteArray VFileList::getMimeData(const QString &p_format,
                                  const QList<QListWidgetItem *> &p_items) const
{
    Q_UNUSED(p_format);
    Q_ASSERT(p_format ==ClipboardConfig::c_format);

    QJsonArray files;
    for (int i = 0; i < p_items.size(); ++i) {
        VNoteFile *file = getVFile(p_items[i]);
        files.append(file->fetchPath());
    }

    QJsonObject obj;
    obj[ClipboardConfig::c_type] = (int)ClipboardOpType::CopyFile;
    obj[ClipboardConfig::c_files] = files;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

void VFileList::setEnableSplitOut(bool p_enabled)
{
    m_splitBtn->setChecked(p_enabled);
}

void VFileList::openCurrentItemViaDefaultProgram()
{
    QListWidgetItem *item = fileList->currentItem();
    if (item) {
        VNoteFile *file = getVFile(item);
        if (file
            && (!g_config->getCloseBeforeExternalEditor()
                || !editArea->isFileOpened(file)
                || editArea->closeFile(file, false))) {
            QUrl url = QUrl::fromLocalFile(file->fetchPath());
            QDesktopServices::openUrl(url);
        }
    }
}
