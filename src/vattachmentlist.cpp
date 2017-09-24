#include "vattachmentlist.h"

#include <QtWidgets>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vbuttonwithwidget.h"
#include "vnote.h"
#include "vmainwindow.h"
#include "dialog/vconfirmdeletiondialog.h"
#include "dialog/vsortdialog.h"

extern VConfigManager *g_config;
extern VNote *g_vnote;

VAttachmentList::VAttachmentList(QWidget *p_parent)
    : QWidget(p_parent), m_file(NULL)
{
    setupUI();

    initActions();

    updateContent();
}

void VAttachmentList::setupUI()
{
    m_addBtn = new QPushButton(QIcon(":/resources/icons/add_attachment.svg"), "");
    m_addBtn->setToolTip(tr("Add"));
    m_addBtn->setProperty("FlatBtn", true);
    connect(m_addBtn, &QPushButton::clicked,
            this, &VAttachmentList::addAttachment);

    m_clearBtn = new QPushButton(QIcon(":/resources/icons/clear_attachment.svg"), "");
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
                                                     "You could find deleted files in the recycle bin "
                                                     "of this notebook.<br>The operation is IRREVERSIBLE!")
                                                    .arg(g_config->c_warningTextStyle)
                                                    .arg(g_config->c_dataTextStyle)
                                                    .arg(m_file->fetchAttachmentFolderPath()),
                                                  QMessageBox::Ok | QMessageBox::Cancel,
                                                  QMessageBox::Ok,
                                                  g_vnote->getMainWindow(),
                                                  MessageBoxType::Danger);
                    if (ret == QMessageBox::Ok) {
                        if (!m_file->deleteAttachments()) {
                            VUtils::showMessage(QMessageBox::Warning,
                                                tr("Warning"),
                                                tr("Fail to clear attachments of note <span style=\"%1\">%2</span>.")
                                                  .arg(g_config->c_dataTextStyle)
                                                  .arg(m_file->getName()),
                                                tr("Please maintain the configureation file manually."),
                                                QMessageBox::Ok,
                                                QMessageBox::Ok,
                                                g_vnote->getMainWindow());
                        }

                        m_attachmentList->clear();
                    }
                }
            });

    m_locateBtn = new QPushButton(QIcon(":/resources/icons/locate_attachment.svg"), "");
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

void VAttachmentList::initActions()
{
    m_openAct = new QAction(tr("&Open"), this);
    m_openAct->setToolTip(tr("Open current attachment file"));
    connect(m_openAct, &QAction::triggered,
            this, [this]() {
                QListWidgetItem *item = m_attachmentList->currentItem();
                handleItemActivated(item);
            });

    m_deleteAct = new QAction(QIcon(":/resources/icons/delete_attachment.svg"),
                              tr("&Delete"),
                              this);
    m_deleteAct->setToolTip(tr("Delete selected attachments"));
    connect(m_deleteAct, &QAction::triggered,
            this, &VAttachmentList::deleteSelectedItems);

    m_sortAct = new QAction(QIcon(":/resources/icons/sort.svg"),
                            tr("&Sort"),
                            this);
    m_sortAct->setToolTip(tr("Sort attachments manually"));
    connect(m_sortAct, &QAction::triggered,
            this, &VAttachmentList::sortItems);
}

void VAttachmentList::setFile(VNoteFile *p_file)
{
    m_file = p_file;
    updateContent();
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
    } else {
        m_numLabel->setText("");
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
    QStringList files = QFileDialog::getOpenFileNames(g_vnote->getMainWindow(),
                                                      tr("Select Files As Attachments"),
                                                      lastPath);
    if (files.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(files[0]).path();

    int addedFiles = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (!m_file->addAttachment(files[i])) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to add attachment %1 for note <span style=\"%2\">%3</span>.")
                                  .arg(files[i])
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_vnote->getMainWindow());
        } else {
            ++addedFiles;
        }
    }

    updateContent();

    if (addedFiles > 0) {
        g_vnote->getMainWindow()->showStatusMessage(tr("Added %1 %2 as attachments")
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

    if (item) {
        if (!item->isSelected()) {
            m_attachmentList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
        }

        if (m_attachmentList->selectedItems().size() == 1) {
            menu.addAction(m_openAct);
        }

        menu.addAction(m_deleteAct);
    }

    m_attachmentList->update();

    if (m_file->getAttachments().size() > 1) {
        if (!menu.actions().isEmpty()) {
            menu.addSeparator();
        }

        menu.addAction(m_sortAct);
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
    QVector<QString> names;
    const QList<QListWidgetItem *> selectedItems = m_attachmentList->selectedItems();

    if (selectedItems.isEmpty()) {
        return;
    }

    for (auto const & item : selectedItems) {
        names.push_back(item->text());
    }

    QString info = tr("Are you sure to delete these attachments of note "
                      "<span style=\"%1\">%2</span>? "
                      "You could find deleted files in the recycle "
                      "bin of this notebook.<br>"
                      "Click \"Cancel\" to leave them untouched.")
                     .arg(g_config->c_dataTextStyle).arg(m_file->getName());
    VConfirmDeletionDialog dialog(tr("Confirm Deleting Attachments"),
                                  info,
                                  names,
                                  false,
                                  false,
                                  false,
                                  g_vnote->getMainWindow());
    if (dialog.exec()) {
        names = dialog.getConfirmedFiles();

        if (!m_file->deleteAttachments(names)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to delete attachments of note <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(m_file->getName()),
                                tr("Please maintain the configureation file manually."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                g_vnote->getMainWindow());
        }

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
                       tr("Sort attachments in the configuration file."),
                       g_vnote->getMainWindow());
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(1);
    tree->header()->setStretchLastSection(true);
    QStringList headers;
    headers << tr("Name");
    tree->setHeaderLabels(headers);

    for (int i = 0; i < attas.size(); ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(tree, QStringList(attas[i].m_name));

        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        int cnt = tree->topLevelItemCount();
        Q_ASSERT(cnt == attas.size());
        QVector<int> sortedIdx(cnt, -1);
        for (int i = 0; i < cnt; ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            Q_ASSERT(item);
            sortedIdx[i] = item->data(0, Qt::UserRole).toInt();
        }

        m_file->sortAttachments(sortedIdx);
    }
}

void VAttachmentList::handleListItemCommitData(QWidget *p_itemEdit)
{
    QString text = reinterpret_cast<QLineEdit *>(p_itemEdit)->text();
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
            VUtils::showMessage(QMessageBox::Information,
                                tr("Rename Attachment"),
                                tr("Fail to rename attachment <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(oldText),
                                "",
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
            // Recover to old name.
            item->setText(oldText);
        } else {
            // Change the data.
            item->setData(Qt::UserRole, text);
        }
    }
}
