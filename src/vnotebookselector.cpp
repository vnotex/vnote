#include "vnotebookselector.h"
#include <QDebug>
#include <QJsonObject>
#include <QListWidget>
#include <QAction>
#include <QMenu>
#include <QGuiApplication>
#include <QScreen>
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

extern VConfigManager vconfig;

const int VNotebookSelector::c_notebookStartIdx = 1;

VNotebookSelector::VNotebookSelector(VNote *vnote, QWidget *p_parent)
    : QComboBox(p_parent), m_vnote(vnote), m_notebooks(m_vnote->getNotebooks()),
      m_editArea(NULL), m_lastValidIndex(-1)
{
    m_listWidget = new QListWidget(this);
    m_listWidget->setItemDelegate(new VNoFocusItemDelegate(this));
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &VNotebookSelector::requestPopupListContextMenu);

    setModel(m_listWidget->model());
    setView(m_listWidget);

    m_listWidget->viewport()->installEventFilter(this);

    initActions();

    connect(this, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurIndexChanged(int)));
    connect(this, SIGNAL(activated(int)),
            this, SLOT(handleItemActivated(int)));
}

void VNotebookSelector::initActions()
{
    m_deleteNotebookAct = new QAction(QIcon(":/resources/icons/delete_notebook.svg"),
                                      tr("&Delete"), this);
    m_deleteNotebookAct->setStatusTip(tr("Delete current notebook"));
    connect(m_deleteNotebookAct, SIGNAL(triggered(bool)),
            this, SLOT(deleteNotebook()));

    m_notebookInfoAct = new QAction(QIcon(":/resources/icons/notebook_info.svg"),
                                    tr("&Info"), this);
    m_notebookInfoAct->setStatusTip(tr("View and edit current notebook's information"));
    connect(m_notebookInfoAct, SIGNAL(triggered(bool)),
            this, SLOT(editNotebookInfo()));
}

void VNotebookSelector::updateComboBox()
{
    int index = vconfig.getCurNotebookIndex();

    disconnect(this, SIGNAL(currentIndexChanged(int)),
               this, SLOT(handleCurIndexChanged(int)));
    clear();
    m_listWidget->clear();

    insertAddNotebookItem();

    for (int i = 0; i < m_notebooks.size(); ++i) {
        addNotebookItem(m_notebooks[i]->getName());
    }
    setCurrentIndex(-1);
    connect(this, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurIndexChanged(int)));

    if (m_notebooks.isEmpty()) {
        vconfig.setCurNotebookIndex(-1);
        setCurrentIndex(0);
    } else {
        setCurrentIndexNotebook(index);
    }
    qDebug() << "notebooks" << m_notebooks.size() << "current index" << index;
}

void VNotebookSelector::setCurrentIndexNotebook(int p_index)
{
    if (p_index > -1) {
        p_index += c_notebookStartIdx;
    }
    setCurrentIndex(p_index);
}

void VNotebookSelector::insertAddNotebookItem()
{
    QListWidgetItem *item = new QListWidgetItem();
    item->setIcon(QIcon(":/resources/icons/create_notebook.svg"));
    item->setText(tr("Add Notebook"));
    QFont font;
    font.setItalic(true);
    item->setData(Qt::FontRole, font);
    item->setToolTip(tr("Create or import a notebook"));
    m_listWidget->insertItem(0, item);
}

void VNotebookSelector::handleCurIndexChanged(int p_index)
{
    qDebug() << "current index changed" << p_index << "startIdx" << c_notebookStartIdx;
    VNotebook *nb = NULL;
    if (p_index > -1) {
        if (p_index < c_notebookStartIdx) {
            // Click a special action item.
            if (m_listWidget->count() == c_notebookStartIdx) {
                // There is no regular notebook item. Just let it be selected.
                p_index = -1;
            } else {
                // handleItemActivated() will handle the logics.
                return;
            }
        } else {
            int nbIdx = p_index - c_notebookStartIdx;
            Q_ASSERT(nbIdx >= 0);
            nb = m_notebooks[nbIdx];
        }
    }
    m_lastValidIndex = p_index;
    QString tooltip;
    if (p_index > -1) {
        p_index -= c_notebookStartIdx;
        tooltip = nb->getName();
    }
    setToolTip(tooltip);
    vconfig.setCurNotebookIndex(p_index);
    emit curNotebookChanged(nb);
}

void VNotebookSelector::handleItemActivated(int p_index)
{
    if (p_index > -1 && p_index < c_notebookStartIdx) {
        // Click a special action item
        if (m_lastValidIndex > -1) {
            setCurrentIndex(m_lastValidIndex);
        }
        newNotebook();
    }
}

void VNotebookSelector::update()
{
    updateComboBox();
}

bool VNotebookSelector::newNotebook()
{
    QString info;
    QString defaultName("new_notebook");
    QString defaultPath;

    do {
        VNewNotebookDialog dialog(tr("Add Notebook"), info, defaultName,
                                  defaultPath, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString path = dialog.getPathInput();
            if (findNotebook(name)) {
                info = tr("Name already exists. Please choose another name.");
                defaultName = name;
                defaultPath = path;
                continue;
            }
            createNotebook(name, path, dialog.getImportCheck());
            return true;
        }
        break;
    } while (true);
    return false;
}

VNotebook *VNotebookSelector::findNotebook(const QString &p_name)
{
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks[i]->getName() == p_name) {
            return m_notebooks[i];
        }
    }
    return NULL;
}

void VNotebookSelector::createNotebook(const QString &p_name, const QString &p_path, bool p_import)
{
    VNotebook *nb = VNotebook::createNotebook(p_name, p_path, p_import, m_vnote);
    m_notebooks.append(nb);
    vconfig.setNotebooks(m_notebooks);

    addNotebookItem(nb->getName());
    setCurrentIndexNotebook(m_notebooks.size() - 1);
}

void VNotebookSelector::deleteNotebook()
{
    QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    Q_ASSERT(items.size() == 1);
    QListWidgetItem *item = items[0];
    int index = indexOfListItem(item);

    VNotebook *notebook = getNotebookFromComboIndex(index);
    Q_ASSERT(notebook);

    VDeleteNotebookDialog dialog(tr("Delete Notebook"), notebook->getName(), notebook->getPath(), this);
    if (dialog.exec() == QDialog::Accepted) {
        bool deleteFiles = dialog.getDeleteFiles();
        m_editArea->closeFile(notebook, true);
        deleteNotebook(notebook, deleteFiles);
    }
}

void VNotebookSelector::deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles)
{
    int idx = indexOfNotebook(p_notebook);

    m_notebooks.remove(idx);
    vconfig.setNotebooks(m_notebooks);

    removeNotebookItem(idx);

    VNotebook::deleteNotebook(p_notebook, p_deleteFiles);
}

int VNotebookSelector::indexOfNotebook(const VNotebook *p_notebook)
{
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks[i] == p_notebook) {
            return i;
        }
    }
    return -1;
}


void VNotebookSelector::editNotebookInfo()
{
    QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    Q_ASSERT(items.size() == 1);
    QListWidgetItem *item = items[0];
    int index = indexOfListItem(item);

    VNotebook *notebook = getNotebookFromComboIndex(index);
    QString info;
    QString curName = notebook->getName();
    QString defaultPath = notebook->getPath();
    QString defaultName(curName);
    do {
        VNotebookInfoDialog dialog(tr("Notebook Information"), info, defaultName,
                                   defaultPath, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (findNotebook(name)) {
                info = "Name already exists. Please choose another name.";
                defaultName = name;
                continue;
            }
            notebook->rename(name);
            updateComboBoxItem(index, name);
            vconfig.setNotebooks(m_notebooks);
            emit notebookUpdated(notebook);
        }
        break;
    } while (true);
}

void VNotebookSelector::addNotebookItem(const QString &p_name)
{
    QListWidgetItem *item = new QListWidgetItem(m_listWidget);
    item->setText(p_name);
    item->setToolTip(p_name);
    item->setIcon(QIcon(":/resources/icons/notebook_item.svg"));
}

void VNotebookSelector::removeNotebookItem(int p_index)
{
    QListWidgetItem *item = m_listWidget->item(p_index + c_notebookStartIdx);
    m_listWidget->removeItemWidget(item);
    delete item;
}

void VNotebookSelector::updateComboBoxItem(int p_index, const QString &p_name)
{
    QListWidgetItem *item = m_listWidget->item(p_index);
    item->setText(p_name);
    item->setToolTip(p_name);
}

void VNotebookSelector::requestPopupListContextMenu(QPoint p_pos)
{
    QModelIndex index = m_listWidget->indexAt(p_pos);
    if (!index.isValid() || index.row() < c_notebookStartIdx) {
        return;
    }

    QListWidgetItem *item = m_listWidget->itemAt(p_pos);
    Q_ASSERT(item);
    m_listWidget->clearSelection();
    item->setSelected(true);

    QMenu menu(this);
    menu.addAction(m_deleteNotebookAct);
    menu.addAction(m_notebookInfoAct);

    menu.exec(m_listWidget->mapToGlobal(p_pos));
}

bool VNotebookSelector::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent *>(event)->button() == Qt::RightButton) {
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

int VNotebookSelector::indexOfListItem(const QListWidgetItem *p_item)
{
    int nrItems = m_listWidget->count();
    for (int i = 0; i < nrItems; ++i) {
        if (m_listWidget->item(i) == p_item) {
            return i;
        }
    }
    return -1;
}

bool VNotebookSelector::locateNotebook(const VNotebook *p_notebook)
{
    if (p_notebook) {
        for (int i = 0; i < m_notebooks.size(); ++i) {
            if (m_notebooks[i] == p_notebook) {
                setCurrentIndexNotebook(i);
                return true;
            }
        }
    }
    return false;
}

void VNotebookSelector::showPopup()
{
    if (count() <= c_notebookStartIdx) {
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
