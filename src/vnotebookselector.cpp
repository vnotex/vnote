#include "vnotebookselector.h"
#include <QDebug>
#include <QJsonObject>
#include <QListWidget>
#include <QAction>
#include <QMenu>
#include <QGuiApplication>
#include <QScreen>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>

#include "vnotebook.h"
#include "vconfigmanager.h"
#include "dialog/vnewnotebookdialog.h"
#include "dialog/vnotebookinfodialog.h"
#include "dialog/vdeletenotebookdialog.h"
#include "vnotebook.h"
#include "vdirectory.h"
#include "utils/vutils.h"
#include "vnote.h"
#include "veditarea.h"
#include "vnofocusitemdelegate.h"
#include "vmainwindow.h"
#include "utils/vimnavigationforwidget.h"
#include "utils/viconutils.h"
#include "dialog/vsortdialog.h"

extern VConfigManager *g_config;

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

VNotebookSelector::VNotebookSelector(QWidget *p_parent)
    : QComboBox(p_parent),
      VNavigationMode(),
      m_notebooks(g_vnote->getNotebooks()),
      m_lastValidIndex(-1),
      m_muted(false),
      m_naviLabel(NULL)
{
    m_listWidget = new QListWidget(this);
    m_listWidget->setItemDelegate(new VNoFocusItemDelegate(this));
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &VNotebookSelector::popupListContextMenuRequested);

    setModel(m_listWidget->model());
    setView(m_listWidget);

    m_listWidget->viewport()->installEventFilter(this);
    m_listWidget->installEventFilter(this);

    connect(this, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurIndexChanged(int)));
}

void VNotebookSelector::updateComboBox()
{
    m_muted = true;

    clear();

    m_listWidget->clear();

    insertAddNotebookItem();

    for (int i = 0; i < m_notebooks.size(); ++i) {
        addNotebookItem(m_notebooks[i]);
    }

    setCurrentIndex(-1);

    m_muted = false;

    if (m_notebooks.isEmpty()) {
        g_config->setCurNotebookIndex(-1);
        setCurrentIndex(0);
    }
}

void VNotebookSelector::restoreCurrentNotebook()
{
    int index = g_config->getCurNotebookIndex();
    if (index < 0 || index >= m_notebooks.size()) {
        index = 0;
    }

    const VNotebook *nb = NULL;
    if (index < m_notebooks.size()) {
        nb = m_notebooks[index];
    }

    if (nb) {
        setCurrentItemToNotebook(nb);
    }
}

void VNotebookSelector::setCurrentItemToNotebook(const VNotebook *p_notebook)
{
    setCurrentIndex(itemIndexOfNotebook(p_notebook));
}

int VNotebookSelector::itemIndexOfNotebook(const VNotebook *p_notebook) const
{
    if (!p_notebook) {
        return -1;
    }

    qulonglong ptr = (qulonglong)p_notebook;
    int cnt = m_listWidget->count();
    for (int i = 0; i < cnt; ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        if (item->data(Qt::UserRole).toULongLong() == ptr) {
            return i;
        }
    }

    return -1;
}

void VNotebookSelector::insertAddNotebookItem()
{
    QListWidgetItem *item = new QListWidgetItem();
    item->setIcon(VIconUtils::comboBoxIcon(":/resources/icons/create_notebook.svg"));
    item->setText(tr("Add Notebook"));
    QFont font;
    font.setItalic(true);
    item->setData(Qt::FontRole, font);
    item->setToolTip(tr("Create or import a notebook"));

    m_listWidget->insertItem(0, item);
}

void VNotebookSelector::handleCurIndexChanged(int p_index)
{
    if (m_muted) {
        return;
    }

    QString tooltip = tr("View and edit notebooks");
    VNotebook *nb = NULL;
    if (p_index > -1) {
        nb = getNotebook(p_index);
        if (!nb) {
            // Add notebook.
            setToolTip(tooltip);

            if (m_lastValidIndex != p_index && m_lastValidIndex > -1) {
                setCurrentIndex(m_lastValidIndex);
            }

            if (!m_notebooks.isEmpty()) {
                newNotebook();
            }

            return;
        }
    }

    m_lastValidIndex = p_index;

    int nbIdx = -1;
    if (nb) {
        tooltip = nb->getName();

        nbIdx = m_notebooks.indexOf(nb);
        Q_ASSERT(nbIdx > -1);
    }

    g_config->setCurNotebookIndex(nbIdx);

    setToolTip(tooltip);

    emit curNotebookChanged(nb);
}

void VNotebookSelector::update()
{
    updateComboBox();
}

bool VNotebookSelector::newNotebook()
{
    QString info(tr("Please type the name of the notebook and "
                    "choose a folder as the Root Folder of the notebook."));
    info += "\n";
    info += tr("* The root folder should be used EXCLUSIVELY by VNote and "
               "it is recommended to be EMPTY.");
    info += "\n";
    info += tr("* A previously created notebook could be imported into VNote "
               "by choosing its root folder.");
    info += "\n";
    info += tr("* When a non-empty folder is chosen, VNote will create a notebook "
               "based on the folders and files in it recursively.");

    // Use empty default name and path to let the dialog to auto generate a name
    // under the default VNote notebook folder.
    VNewNotebookDialog dialog(tr("Add Notebook"),
                              info,
                              "",
                              "",
                              m_notebooks,
                              this);
    if (dialog.exec() == QDialog::Accepted) {
        bool isImport = dialog.isImportExistingNotebook();
        if(dialog.isImportExternalProject()) {
            QString msg;
            bool ret = VNotebook::buildNotebook(dialog.getNameInput(),
                                                dialog.getPathInput(),
                                                dialog.getImageFolder(),
                                                dialog.getAttachmentFolder(),
                                                &msg);

            QList<QString> suffixes = g_config->getDocSuffixes()[(int)DocType::Markdown];
            QString sufs;
            for (auto const & suf : suffixes) {
                if (sufs.isEmpty()) {
                    sufs = "*." + suf;
                } else {
                    sufs += ",*." + suf;
                }
            }

            QString info = ret ? tr("Successfully build notebook recursively (%1).").arg(sufs)
                               : tr("Fail to build notebook recursively.");
            if (!ret || !msg.isEmpty()) {
                VUtils::showMessage(ret ? QMessageBox::Information : QMessageBox::Warning,
                                    ret ? tr("Information") : tr("Warning"),
                                    info,
                                    msg,
                                    QMessageBox::Ok,
                                    QMessageBox::Ok,
                                    this);
            }

            if (!ret) {
                return false;
            }

            isImport = true;
        }

        createNotebook(dialog.getNameInput(),
                       dialog.getPathInput(),
                       isImport,
                       dialog.getImageFolder(),
                       dialog.getAttachmentFolder());

        emit notebookCreated(dialog.getNameInput(), isImport);
        return true;
    }

    return false;
}

void VNotebookSelector::createNotebook(const QString &p_name,
                                       const QString &p_path,
                                       bool p_import,
                                       const QString &p_imageFolder,
                                       const QString &p_attachmentFolder)
{
    VNotebook *nb = VNotebook::createNotebook(p_name,
                                              p_path,
                                              p_import,
                                              p_imageFolder,
                                              p_attachmentFolder,
                                              g_vnote);
    if (!nb) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to create notebook "
                               "<span style=\"%1\">%2</span> in <span style=\"%1\">%3</span>.")
                            .arg(g_config->c_dataTextStyle).arg(p_name).arg(p_path), "",
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return;
    }

    m_notebooks.append(nb);
    g_config->setNotebooks(m_notebooks);

    addNotebookItem(nb);
    setCurrentItemToNotebook(nb);
}

void VNotebookSelector::deleteNotebook()
{
    QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    Q_ASSERT(items.size() == 1);

    VNotebook *notebook = getNotebook(items[0]);
    Q_ASSERT(notebook);

    VDeleteNotebookDialog dialog(tr("Delete Notebook"), notebook, this);
    if (dialog.exec() == QDialog::Accepted) {
        bool deleteFiles = dialog.getDeleteFiles();
        g_mainWin->getEditArea()->closeFile(notebook, true);
        deleteNotebook(notebook, deleteFiles);
    }
}

void VNotebookSelector::deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles)
{
    Q_ASSERT(p_notebook);

    m_notebooks.removeOne(p_notebook);
    g_config->setNotebooks(m_notebooks);

    int idx = itemIndexOfNotebook(p_notebook);
    QListWidgetItem *item = m_listWidget->takeItem(idx);
    Q_ASSERT(item);
    delete item;

    QString name(p_notebook->getName());
    QString path(p_notebook->getPath());
    bool ret = VNotebook::deleteNotebook(p_notebook, p_deleteFiles);
    if (!ret) {
        // Notebook could not be deleted completely.
        int cho = VUtils::showMessage(QMessageBox::Information,
                                      tr("Delete Notebook Folder From Disk"),
                                      tr("Fail to delete the root folder of notebook "
                                         "<span style=\"%1\">%2</span> from disk. You may open "
                                         "the folder and check it manually.")
                                        .arg(g_config->c_dataTextStyle).arg(name),
                                      "",
                                      QMessageBox::Open | QMessageBox::Ok,
                                      QMessageBox::Ok,
                                      this);
        if (cho == QMessageBox::Open) {
            // Open the notebook location.
            QUrl url = QUrl::fromLocalFile(path);
            QDesktopServices::openUrl(url);
        }
    }

    if (m_notebooks.isEmpty()) {
        m_muted = true;
        setCurrentIndex(0);
        m_muted = false;
    }
}

void VNotebookSelector::editNotebookInfo()
{
    QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    Q_ASSERT(items.size() == 1);

    VNotebook *notebook = getNotebook(items[0]);
    VNotebookInfoDialog dialog(tr("Notebook Information"),
                               "",
                               notebook,
                               m_notebooks,
                               this);
    if (dialog.exec() == QDialog::Accepted) {
        bool updated = false;
        bool configUpdated = false;
        QString name = dialog.getName();
        if (name != notebook->getName()) {
            updated = true;
            notebook->rename(name);
            g_config->setNotebooks(m_notebooks);
        }

        QString imageFolder = dialog.getImageFolder();
        if (imageFolder != notebook->getImageFolderConfig()) {
            configUpdated = true;
            notebook->setImageFolder(imageFolder);
        }

        if (configUpdated) {
            updated = true;
            notebook->writeConfigNotebook();
        }

        if (updated) {
            fillItem(items[0], notebook);
            emit notebookUpdated(notebook);
        }
    }
}

void VNotebookSelector::addNotebookItem(const VNotebook *p_notebook)
{
    QListWidgetItem *item = new QListWidgetItem(m_listWidget);
    fillItem(item, p_notebook);
}

void VNotebookSelector::fillItem(QListWidgetItem *p_item,
                                 const VNotebook *p_notebook) const
{
    p_item->setText(p_notebook->getName());
    p_item->setToolTip(p_notebook->getName());
    p_item->setIcon(VIconUtils::comboBoxIcon(":/resources/icons/notebook_item.svg"));
    p_item->setData(Qt::UserRole, (qulonglong)p_notebook);
}


void VNotebookSelector::popupListContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_listWidget->itemAt(p_pos);
    if (!item) {
        return;
    }

    const VNotebook *nb = getNotebook(item);
    if (!nb) {
        return;
    }

    m_listWidget->clearSelection();
    item->setSelected(true);

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QAction *deleteNotebookAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/delete_notebook.svg"),
                                             tr("&Delete"),
                                             &menu);
    deleteNotebookAct->setToolTip(tr("Delete current notebook"));
    connect(deleteNotebookAct, SIGNAL(triggered(bool)),
            this, SLOT(deleteNotebook()));
    menu.addAction(deleteNotebookAct);

    if (m_notebooks.size() > 1) {
        QAction *sortAct = new QAction(VIconUtils::menuIcon(":/resources/icons/sort.svg"),
                                       tr("&Sort"),
                                       &menu);
        sortAct->setToolTip(tr("Sort notebooks"));
        connect(sortAct, SIGNAL(triggered(bool)),
                this, SLOT(sortItems()));
        menu.addAction(sortAct);
    }

    if (nb->isValid()) {
        menu.addSeparator();

        QAction *recycleBinAct = new QAction(VIconUtils::menuIcon(":/resources/icons/recycle_bin.svg"),
                                             tr("&Recycle Bin"),
                                             &menu);
        recycleBinAct->setToolTip(tr("Open the recycle bin of this notebook"));
        connect(recycleBinAct, &QAction::triggered,
                this, [this]() {
                    QList<QListWidgetItem *> items = this->m_listWidget->selectedItems();
                    if (items.isEmpty()) {
                        return;
                    }

                    Q_ASSERT(items.size() == 1);
                    VNotebook *notebook = getNotebook(items[0]);
                    QUrl url = QUrl::fromLocalFile(notebook->getRecycleBinFolderPath());
                    QDesktopServices::openUrl(url);
                });

        menu.addAction(recycleBinAct);

        QAction *emptyRecycleBinAct = new QAction(VIconUtils::menuDangerIcon(":/resources/icons/empty_recycle_bin.svg"),
                                                  tr("&Empty Recycle Bin"),
                                                  &menu);
        emptyRecycleBinAct->setToolTip(tr("Empty the recycle bin of this notebook"));
        connect(emptyRecycleBinAct, &QAction::triggered,
                this, [this]() {
                    QList<QListWidgetItem *> items = this->m_listWidget->selectedItems();
                    if (items.isEmpty()) {
                        return;
                    }

                    Q_ASSERT(items.size() == 1);
                    VNotebook *notebook = getNotebook(items[0]);
                    QString binPath = notebook->getRecycleBinFolderPath();

                    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                                  tr("Are you sure to empty recycle bin of notebook "
                                                     "<span style=\"%1\">%2</span>?")
                                                    .arg(g_config->c_dataTextStyle)
                                                    .arg(notebook->getName()),
                                                  tr("<span style=\"%1\">WARNING</span>: "
                                                     "VNote will delete all the files in directory "
                                                     "<span style=\"%2\">%3</span>."
                                                     "<br>It may be UNRECOVERABLE!")
                                                    .arg(g_config->c_warningTextStyle)
                                                    .arg(g_config->c_dataTextStyle)
                                                    .arg(binPath),
                                                  QMessageBox::Ok | QMessageBox::Cancel,
                                                  QMessageBox::Ok,
                                                  this,
                                                  MessageBoxType::Danger);
                    if (ret == QMessageBox::Ok) {
                        QString info;
                        if (VUtils::emptyDirectory(notebook, binPath, true)) {
                            info = tr("Successfully emptied recycle bin of notebook "
                                      "<span style=\"%1\">%2</span>!")
                                     .arg(g_config->c_dataTextStyle)
                                     .arg(notebook->getName());
                        } else {
                            info = tr("Fail to empty recycle bin of notebook "
                                      "<span style=\"%1\">%2</span>!")
                                     .arg(g_config->c_dataTextStyle)
                                     .arg(notebook->getName());
                        }

                        VUtils::showMessage(QMessageBox::Information,
                                            tr("Information"),
                                            info,
                                            "",
                                            QMessageBox::Ok,
                                            QMessageBox::Ok,
                                            this);
                    }
                });
        menu.addAction(emptyRecycleBinAct);
    }

    menu.addSeparator();

    QAction *openLocationAct = new QAction(VIconUtils::menuIcon(":/resources/icons/open_location.svg"),
                                           tr("&Open Notebook Location"),
                                           &menu);
    openLocationAct->setToolTip(tr("Explore the root folder of this notebook in operating system"));
    connect(openLocationAct, &QAction::triggered,
            this, [this]() {
                QList<QListWidgetItem *> items = this->m_listWidget->selectedItems();
                if (items.isEmpty()) {
                    return;
                }

                Q_ASSERT(items.size() == 1);
                VNotebook *notebook = getNotebook(items[0]);
                QUrl url = QUrl::fromLocalFile(notebook->getPath());
                QDesktopServices::openUrl(url);
            });
    menu.addAction(openLocationAct);

    if (nb->isValid()) {
        QAction *notebookInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/notebook_info.svg"),
                                               tr("&Info (Rename)"),
                                               &menu);
        notebookInfoAct->setToolTip(tr("View and edit current notebook's information"));
        connect(notebookInfoAct, SIGNAL(triggered(bool)),
                this, SLOT(editNotebookInfo()));
        menu.addAction(notebookInfoAct);
    }

    menu.exec(m_listWidget->mapToGlobal(p_pos));
}

bool VNotebookSelector::eventFilter(QObject *watched, QEvent *event)
{
    QEvent::Type type = event->type();
    if (type == QEvent::KeyPress && watched == m_listWidget) {
        if (handlePopupKeyPress(static_cast<QKeyEvent *>(event))) {
            return true;
        }
    } else if (type == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent *>(event)->button() == Qt::RightButton) {
            return true;
        }
    }

    return QComboBox::eventFilter(watched, event);
}

bool VNotebookSelector::locateNotebook(const VNotebook *p_notebook)
{
    bool ret = false;
    int index = itemIndexOfNotebook(p_notebook);
    if (index > -1) {
        setCurrentIndex(index);
        ret = true;
    }

    return ret;
}

void VNotebookSelector::showPopup()
{
    if (m_notebooks.isEmpty()) {
        // No normal notebook items. Just add notebook.
        newNotebook();
        return;
    }

    resizeListWidgetToContent();
    QComboBox::showPopup();
}

void VNotebookSelector::resizeListWidgetToContent()
{
    static QRect screenRect = QGuiApplication::primaryScreen()->geometry();
    static int maxMinWidth = screenRect.width() < 400 ? screenRect.width() : screenRect.width() / 2;
    static int maxMinHeight = screenRect.height() < 400 ? screenRect.height() : screenRect.height() / 2;

    int minWidth = 0;
    int minHeight = 0;
    if (m_listWidget->count() > 0) {
        // Width
        minWidth = m_listWidget->sizeHintForColumn(0);
        minWidth = qMin(minWidth, maxMinWidth);

        // Height
        minHeight = m_listWidget->sizeHintForRow(0) * m_listWidget->count() + 10;
        minHeight = qMin(minHeight, maxMinHeight);
    }

    m_listWidget->setMinimumSize(minWidth, minHeight);
}

void VNotebookSelector::registerNavigation(QChar p_majorKey)
{
    Q_ASSERT(!m_naviLabel);
    m_majorKey = p_majorKey;
}

void VNotebookSelector::showNavigation()
{
    if (!isVisible()) {
        return;
    }

    V_ASSERT(!m_naviLabel);
    m_naviLabel = new QLabel(m_majorKey, this);
    m_naviLabel->setStyleSheet(g_vnote->getNavigationLabelStyle(m_majorKey));
    m_naviLabel->show();
    m_naviLabel->move(rect().topRight() - QPoint(m_naviLabel->width() + 2, 2));
}

void VNotebookSelector::hideNavigation()
{
    if (m_naviLabel) {
        delete m_naviLabel;
        m_naviLabel = NULL;
    }
}

bool VNotebookSelector::handleKeyNavigation(int p_key, bool &p_succeed)
{
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (keyChar == m_majorKey) {
        // Hit.
        p_succeed = true;
        ret = true;
        setFocus();
        if (m_naviLabel) {
            showPopup();
        }
    }

    return ret;
}

bool VNotebookSelector::handlePopupKeyPress(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(m_listWidget,
                                                          p_event)) {
        return true;
    }

    return false;
}

VNotebook *VNotebookSelector::getNotebook(int p_itemIdx) const
{
    VNotebook *nb = NULL;
    QListWidgetItem *item = m_listWidget->item(p_itemIdx);
    if (item) {
        nb = (VNotebook *)item->data(Qt::UserRole).toULongLong();
    }

    return nb;
}

VNotebook *VNotebookSelector::getNotebook(const QListWidgetItem *p_item) const
{
    if (p_item) {
        return (VNotebook *)p_item->data(Qt::UserRole).toULongLong();
    }

    return NULL;
}

VNotebook *VNotebookSelector::currentNotebook() const
{
    return getNotebook(currentIndex());
}

void VNotebookSelector::sortItems()
{
    if (m_notebooks.size() < 2) {
        return;
    }

    VSortDialog dialog(tr("Sort Notebooks"),
                       tr("Sort notebooks in the configuration file."),
                       this);
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(1);
    QStringList headers;
    headers << tr("Name");
    tree->setHeaderLabels(headers);
    for (int i = 0; i < m_notebooks.size(); ++i) {
        QStringList cols;
        cols << m_notebooks[i]->getName();
        QTreeWidgetItem *item = new QTreeWidgetItem(tree, cols);
        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        QVector<QVariant> data = dialog.getSortedData();
        Q_ASSERT(data.size() == m_notebooks.size());
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }

        // Sort m_notebooks.
        auto ori = m_notebooks;
        auto curNotebook = currentNotebook();
        int curNotebookIdx = -1;
        for (int i = 0; i < sortedIdx.size(); ++i) {
            m_notebooks[i] = ori[sortedIdx[i]];
            if (m_notebooks[i] == curNotebook) {
                curNotebookIdx = i;
            }
        }

        Q_ASSERT(ori.size() == m_notebooks.size());
        Q_ASSERT(curNotebookIdx != -1);

        g_config->setNotebooks(m_notebooks);
        g_config->setCurNotebookIndex(curNotebookIdx);

        update();

        setCurrentItemToNotebook(m_notebooks[curNotebookIdx]);
    }
}
