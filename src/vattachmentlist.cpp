#include "vattachmentlist.h"

#include <QtWidgets>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vbuttonwithwidget.h"
#include "vmainwindow.h"
#include "dialog/vconfirmdeletiondialog.h"
#include "dialog/vsortdialog.h"
#include "utils/vimnavigationforwidget.h"
#include "utils/viconutils.h"
#include "vlineedit.h"

extern VConfigManager *g_config;

extern VMainWindow *g_mainWin;

const QString VAttachmentList::c_infoShortcutSequence = "F2";

VAttachmentList::VAttachmentList(QWidget *p_parent)
    : QWidget(p_parent),
      VButtonPopupWidget(this),
      m_initialized(false),
      m_file(NULL)
{
}

void VAttachmentList::setupUI()
{
    m_addBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/add_attachment.svg"), "");
    m_addBtn->setToolTip(tr("Add"));
    m_addBtn->setProperty("FlatBtn", true);
    m_addBtn->setDefault(true);
    connect(m_addBtn, &QPushButton::clicked,
            this, &VAttachmentList::addAttachment);

    m_clearBtn = new QPushButton(VIconUtils::buttonDangerIcon(":/resources/icons/clear_attachment.svg"), "");
    m_clearBtn->setToolTip(tr("Clear"));
    m_clearBtn->setProperty("FlatBtn", true);
    connect(m_clearBtn, &QPushButton::clicked,
            this, [this]() {
                if (m_file && m_attachmentList->count() > 0) {
                    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                                  tr("Are you sure to clear attachments of note "
                                                     "<span style=\"%1\">%2</span>?")
                                                    .arg(g_config->c_dataTextStyle)
                                                    .arg(m_file->getName()),
                                                  tr("<span style=\"%1\">WARNING</span>: "
                                                     "VNote will delete all the files in directory "
                                                     "<span style=\"%2\">%3</span>."
                                                     "Deleted files could be found in the recycle bin "
                                                     "of this note.<br>The operation is IRREVERSIBLE!")
                                                    .arg(g_config->c_warningTextStyle)
                                                    .arg(g_config->c_dataTextStyle)
                                                    .arg(m_file->fetchAttachmentFolderPath()),
                                                  QMessageBox::Ok | QMessageBox::Cancel,
                                                  QMessageBox::Ok,
                                                  g_mainWin,
                                                  MessageBoxType::Danger);
                    if (ret == QMessageBox::Ok) {
                        if (!m_file->deleteAttachments()) {
                            VUtils::showMessage(QMessageBox::Warning,
                                                tr("Warning"),
                                                tr("Fail to clear attachments of note <span style=\"%1\">%2</span>.")
                                                  .arg(g_config->c_dataTextStyle)
                                                  .arg(m_file->getName()),
                                                tr("Please check the attachments folder and "
                                                   "maintain the configuration file manually."),
                                                QMessageBox::Ok,
                                                QMessageBox::Ok,
                                                g_mainWin);
                        }

                        m_attachmentList->clear();

                        updateButtonState();
                    }
                }
            });

    m_locateBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/locate_attachment.svg"), "");
    m_locateBtn->setToolTip(tr("Open Folder"));
    m_locateBtn->setProperty("FlatBtn", true);
    connect(m_locateBtn, &QPushButton::clicked,
            this, [this]() {
                if (m_file && !m_file->getAttachmentFolder().isEmpty()) {
                    QUrl url = QUrl::fromLocalFile(m_file->fetchAttachmentFolderPath());
                    QDesktopServices::openUrl(url);
                }
            });

    m_numLabel = new QLabel();

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addWidget(m_locateBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_numLabel);

    m_attachmentList = new QListWidget;
    m_attachmentList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_attachmentList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_attachmentList->setEditTriggers(QAbstractItemView::SelectedClicked);
    m_attachmentList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_attachmentList->setItemDelegate(&m_listDelegate);
    connect(m_attachmentList, &QListWidget::customContextMenuRequested,
            this, &VAttachmentList::handleContextMenuRequested);
    connect(m_attachmentList, &QListWidget::itemActivated,
            this, &VAttachmentList::handleItemActivated);
    connect(m_attachmentList->itemDelegate(), &QAbstractItemDelegate::commitData,
            this, &VAttachmentList::handleListItemCommitData);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_attachmentList);

    setLayout(mainLayout);
}

void VAttachmentList::setFile(VNoteFile *p_file)
{
    init();

    m_file = p_file;

    updateButtonState();
}

void VAttachmentList::updateContent()
{
    bool enableAdd = true, enableDelete = true, enableClear = true, enableLocate = true;
    m_attachmentList->clear();

    if (!m_file) {
        enableAdd = enableDelete = enableClear = enableLocate = false;
    } else {
        QString folder = m_file->getAttachmentFolder();
        const QVector<VAttachment> &attas = m_file->getAttachments();

        if (folder.isEmpty()) {
            Q_ASSERT(attas.isEmpty());
            enableDelete = enableClear = enableLocate = false;
        } else if (attas.isEmpty()) {
            enableDelete = enableClear = false;
        } else {
            fillAttachmentList(attas);
        }
    }

    m_addBtn->setEnabled(enableAdd);
    m_clearBtn->setEnabled(enableClear);
    m_locateBtn->setEnabled(enableLocate);

    int cnt = m_attachmentList->count();
    if (cnt > 0) {
        m_numLabel->setText(tr("%1 %2").arg(cnt).arg(cnt > 1 ? tr("Files") : tr("File")));
        m_attachmentList->setFocus();
    } else {
        m_numLabel->setText("");

        if (m_file) {
            m_addBtn->setFocus();
        }
    }
}

void VAttachmentList::fillAttachmentList(const QVector<VAttachment> &p_attachments)
{
    Q_ASSERT(m_attachmentList->count() == 0);
    for (int i = 0; i < p_attachments.size(); ++i) {
        const VAttachment &atta = p_attachments[i];
        QListWidgetItem *item = new QListWidgetItem(atta.m_name);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(Qt::UserRole, atta.m_name);

        m_attachmentList->addItem(item);
    }
}

void VAttachmentList::addAttachment()
{
    if (!m_file) {
        return;
    }

    static QString lastPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(g_mainWin,
                                                      tr("Select Files As Attachments"),
                                                      lastPath);
    if (files.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(files[0]).path();

    addAttachments(files);

    updateButtonState();

    updateContent();
}

void VAttachmentList::addAttachments(const QStringList &p_files)
{
    Q_ASSERT(m_file);
    int addedFiles = 0;
    for (int i = 0; i < p_files.size(); ++i) {
        if (!m_file->addAttachment(p_files[i])) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to add attachment %1 for note <span style=\"%2\">%3</span>.")
                                  .arg(p_files[i])
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_mainWin);
        } else {
            ++addedFiles;
        }
    }

    if (addedFiles > 0) {
        g_mainWin->showStatusMessage(tr("%1 %2 added as attachments")
                                     .arg(addedFiles)
                                     .arg(addedFiles > 1 ? tr("files") : tr("file")));
    }
}

void VAttachmentList::handleContextMenuRequested(QPoint p_pos)
{
    // @p_pos is the position in the coordinate of VAttachmentList, no m_attachmentList.
    QListWidgetItem *item = m_attachmentList->itemAt(m_attachmentList->mapFromParent(p_pos));
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (!m_file) {
        return;
    }

    int selectedSize = m_attachmentList->selectedItems().size();
    if (item) {
        if (!item->isSelected()) {
            m_attachmentList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
        }

        if (selectedSize == 1) {
            QAction *openAct = new QAction(tr("&Open"), &menu);
            openAct->setToolTip(tr("Open current attachment file"));
            connect(openAct, &QAction::triggered,
                    this, [this]() {
                        QListWidgetItem *item = m_attachmentList->currentItem();
                        handleItemActivated(item);
                    });
            menu.addAction(openAct);
        }

        QAction *deleteAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/delete_attachment.svg"),
                                         tr("&Delete"),
                                         &menu);
        deleteAct->setToolTip(tr("Delete selected attachments"));
        connect(deleteAct, &QAction::triggered,
                this, &VAttachmentList::deleteSelectedItems);
        menu.addAction(deleteAct);
    }

    m_attachmentList->update();

    if (m_file->getAttachments().size() > 1) {
        if (!menu.actions().isEmpty()) {
            menu.addSeparator();
        }

        QAction *sortAct = new QAction(VIconUtils::menuIcon(":/resources/icons/sort.svg"),
                                       tr("&Sort"),
                                       &menu);
        sortAct->setToolTip(tr("Sort attachments manually"));
        connect(sortAct, &QAction::triggered,
                this, &VAttachmentList::sortItems);
        menu.addAction(sortAct);
    }

    if (selectedSize == 1) {
        menu.addSeparator();

        QAction *fileInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/note_info.svg"),
                                           tr("&Info (Rename)\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)),
                                           &menu);
        fileInfoAct->setToolTip(tr("View and edit current attachment's information"));
        connect(fileInfoAct, &QAction::triggered,
                this, &VAttachmentList::attachmentInfo);
        menu.addAction(fileInfoAct);
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(mapToGlobal(p_pos));
    }
}

void VAttachmentList::handleItemActivated(QListWidgetItem *p_item)
{
    if (p_item) {
        Q_ASSERT(m_file);

        QString name = p_item->text();
        QString folderPath = m_file->fetchAttachmentFolderPath();
        QUrl url = QUrl::fromLocalFile(QDir(folderPath).filePath(name));
        QDesktopServices::openUrl(url);
    }
}

void VAttachmentList::deleteSelectedItems()
{
    QVector<ConfirmItemInfo> items;
    const QList<QListWidgetItem *> selectedItems = m_attachmentList->selectedItems();

    if (selectedItems.isEmpty()) {
        return;
    }

    for (auto const & item : selectedItems) {
        items.push_back(ConfirmItemInfo(item->text(),
                                        item->text(),
                                        "",
                                        NULL));
    }

    QString text = tr("Are you sure to delete these attachments of note "
                      "<span style=\"%1\">%2</span>?")
                     .arg(g_config->c_dataTextStyle).arg(m_file->getName());

    QString info = tr("Deleted files could be found in the recycle "
                      "bin of this note.<br>"
                      "Click \"Cancel\" to leave them untouched.");

    VConfirmDeletionDialog dialog(tr("Confirm Deleting Attachments"),
                                  text,
                                  info,
                                  items,
                                  false,
                                  false,
                                  false,
                                  g_mainWin);
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();

        QVector<QString> names;
        for (auto const & item : items) {
            names.push_back(item.m_name);
        }

        if (!m_file->deleteAttachments(names)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to delete attachments of note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                tr("Please check the attachments folder and "
                                   "maintain the configuration file manually."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_mainWin);
        }

        updateButtonState();

        updateContent();
    }
}

void VAttachmentList::sortItems()
{
    const QVector<VAttachment> &attas = m_file->getAttachments();
    if (attas.size() < 2) {
        return;
    }

    VSortDialog dialog(tr("Sort Attachments"),
                       tr("Sort attachments of note <span style=\"%1\">%2</span> "
                          "in the configuration file.")
                         .arg(g_config->c_dataTextStyle)
                         .arg(m_file->getName()),
                       g_mainWin);
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(1);
    QStringList headers(tr("Name"));
    tree->setHeaderLabels(headers);

    for (int i = 0; i < attas.size(); ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(tree, QStringList(attas[i].m_name));
        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        QVector<QVariant> data = dialog.getSortedData();
        Q_ASSERT(data.size() == attas.size());
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }

        if (!m_file->sortAttachments(sortedIdx)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to sort attachments of note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }
    }
}

void VAttachmentList::handleListItemCommitData(QWidget *p_itemEdit)
{
    QString text = reinterpret_cast<VLineEdit *>(p_itemEdit)->text();
    QListWidgetItem *item = m_attachmentList->currentItem();
    Q_ASSERT(item && item->text() == text);

    QString oldText = item->data(Qt::UserRole).toString();

    if (oldText == text) {
        return;
    }

    bool legalName = true;
    if (text.isEmpty()) {
        legalName = false;
    } else {
        QRegExp reg(VUtils::c_fileNameRegExp);
        if (!reg.exactMatch(text)) {
            legalName = false;
        }
    }

    if (!legalName) {
        // Recover to old name.
        item->setText(oldText);
        return;
    }

    if (!(oldText.toLower() == text.toLower())
        && m_file->findAttachment(text, false) > -1) {
        // Name conflict.
        // Recover to old name.
        item->setText(oldText);
    } else {
        if (!m_file->renameAttachment(oldText, text)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Rename Attachment"),
                                tr("Fail to rename attachment <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(oldText),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_mainWin);
            // Recover to old name.
            item->setText(oldText);
        } else {
            // Change the data.
            item->setData(Qt::UserRole, text);
        }
    }
}

void VAttachmentList::keyPressEvent(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(m_attachmentList,
                                                          p_event,
                                                          this)) {
        return;
    }

    QWidget::keyPressEvent(p_event);
}

bool VAttachmentList::isAcceptDrops() const
{
    return true;
}

bool VAttachmentList::handleDragEnterEvent(QDragEnterEvent *p_event)
{
    if (!m_file) {
        return false;
    }

    if (p_event->mimeData()->hasFormat("text/uri-list")) {
        p_event->acceptProposedAction();
        return true;
    }

    return false;
}

bool VAttachmentList::handleDropEvent(QDropEvent *p_event)
{
    if (!m_file) {
        return false;
    }

    init();

    const QMimeData *mime = p_event->mimeData();
    if (mime->hasFormat("text/uri-list") && mime->hasUrls()) {
        // Add attachments.
        QStringList files;
        QList<QUrl> urls = mime->urls();
        for (int i = 0; i < urls.size(); ++i) {
            QString file;
            if (urls[i].isLocalFile()) {
                file = urls[i].toLocalFile();
                QFileInfo fi(file);
                if (fi.exists() && fi.isFile()) {
                    file = QDir::cleanPath(fi.absoluteFilePath());
                    files.append(file);
                }
            }
        }

        if (!files.isEmpty()) {
            addAttachments(files);

            updateButtonState();
        }

        p_event->acceptProposedAction();
        return true;
    }

    return false;
}

void VAttachmentList::handleAboutToShow()
{
    init();

    updateContent();

    checkAttachments();
}

void VAttachmentList::updateButtonState() const
{
    VButtonWithWidget *btn = getButton();
    Q_ASSERT(btn);
    if (!btn) {
        return;
    }

    int numOfAttachments = -1;
    if (m_file) {
        numOfAttachments = m_file->getAttachments().size();
        if (numOfAttachments == 0) {
            numOfAttachments = -1;
        }
    }

    btn->setBubbleNumber(numOfAttachments);
}

void VAttachmentList::checkAttachments()
{
    if (!m_file) {
        return;
    }

    QVector<QString> missingAttas = m_file->checkAttachments();
    if (missingAttas.isEmpty()) {
        return;
    }

    QVector<ConfirmItemInfo> items;
    for (auto const & atta : missingAttas) {
        items.push_back(ConfirmItemInfo(atta,
                                        atta,
                                        "",
                                        NULL));
    }

    QString text = tr("VNote detects that these attachments of note "
                      "<span style=\"%1\">%2</span> are missing in disk. "
                      "Would you like to remove them from the note?")
                     .arg(g_config->c_dataTextStyle)
                     .arg(m_file->getName());

    QString info = tr("Click \"Cancel\" to leave them untouched.");

    VConfirmDeletionDialog dialog(tr("Confirm Deleting Attachments"),
                                  text,
                                  info,
                                  items,
                                  false,
                                  false,
                                  false,
                                  g_mainWin);
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();

        QVector<QString> names;
        for (auto const & item : items) {
            names.push_back(item.m_name);
        }

        if (!m_file->deleteAttachments(names, true)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to delete attachments of note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                tr("Please check the attachments folder and "
                                   "maintain the configuration file manually."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_mainWin);
        }

        updateButtonState();

        updateContent();
    }
}

void VAttachmentList::showEvent(QShowEvent *p_event)
{
    init();

    QWidget::showEvent(p_event);

    processShowEvent(p_event);
}

void VAttachmentList::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    setupUI();

    QShortcut *infoShortcut = new QShortcut(QKeySequence(c_infoShortcutSequence), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, &VAttachmentList::attachmentInfo);

    updateContent();
}

void VAttachmentList::attachmentInfo()
{
    QListWidgetItem *item = m_attachmentList->currentItem();
    if (!item) {
        return;
    }

    QString oldName = item->data(Qt::UserRole).toString();
    QString name = VUtils::promptForFileName(tr("Attachment Information"),
                                             tr("Rename attachment (%1):").arg(oldName),
                                             oldName,
                                             [this](const QString &p_name) {
                                                if (m_file->findAttachment(p_name, false) > -1) {
                                                    return true;
                                                }

                                                return false;
                                             },
                                             g_mainWin);

    if (name.isEmpty()) {
        return;
    }

    if (!m_file->renameAttachment(oldName, name)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Attachment Information"),
                            tr("Fail to rename attachment <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(oldName),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            g_mainWin);
    } else {
        // Change the data.
        item->setData(Qt::UserRole, name);
        item->setText(name);
    }
}
