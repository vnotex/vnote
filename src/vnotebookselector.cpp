#include "vnotebookselector.h"
#include <QDebug>
#include <QJsonObject>
#include "vnotebook.h"
#include "vconfigmanager.h"
#include "dialog/vnewnotebookdialog.h"
#include "dialog/vnotebookinfodialog.h"
#include "vnotebook.h"
#include "vdirectory.h"
#include "utils/vutils.h"
#include "vnote.h"
#include "veditarea.h"

extern VConfigManager vconfig;

VNotebookSelector::VNotebookSelector(VNote *vnote, QWidget *p_parent)
    : QComboBox(p_parent), m_vnote(vnote), m_notebooks(m_vnote->getNotebooks()),
      m_editArea(NULL)
{
    connect(this, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurIndexChanged(int)));
}

void VNotebookSelector::updateComboBox()
{
    int index = vconfig.getCurNotebookIndex();

    disconnect(this, SIGNAL(currentIndexChanged(int)),
               this, SLOT(handleCurIndexChanged(int)));
    clear();
    for (int i = 0; i < m_notebooks.size(); ++i) {
        addItem(QIcon(":/resources/icons/notebook_item.svg"), m_notebooks[i]->getName());
    }
    setCurrentIndex(-1);
    connect(this, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurIndexChanged(int)));

    if (m_notebooks.isEmpty() && index != -1) {
        index = -1;
        vconfig.setCurNotebookIndex(index);
    }
    setCurrentIndex(index);
    qDebug() << "notebooks" << m_notebooks.size() << "current index" << index;
}

void VNotebookSelector::handleCurIndexChanged(int p_index)
{
    qDebug() << "current index changed" << p_index;
    VNotebook *nb = NULL;
    if (p_index > -1) {
        nb = m_notebooks[p_index];
    }
    vconfig.setCurNotebookIndex(p_index);
    emit curNotebookChanged(nb);
}

void VNotebookSelector::update()
{
    updateComboBox();
}

void VNotebookSelector::newNotebook()
{
    QString info;
    QString defaultName("new_notebook");
    QString defaultPath;
    do {
        VNewNotebookDialog dialog(tr("Create notebook"), info, defaultName,
                                  defaultPath, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString path = dialog.getPathInput();
            if (findNotebook(name)) {
                info = "Name already exists. Please choose another name.";
                defaultName = name;
                defaultPath = path;
                continue;
            }
            createNotebook(name, path);
        }
        break;
    } while (true);
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

void VNotebookSelector::createNotebook(const QString &p_name, const QString &p_path)
{
    VNotebook *nb = VNotebook::createNotebook(p_name, p_path, m_vnote);
    m_notebooks.append(nb);
    vconfig.setNotebooks(m_notebooks);

    addItem(QIcon(":/resources/icons/notebook_item.svg"), nb->getName());
    setCurrentIndex(m_notebooks.size() - 1);
}

void VNotebookSelector::deleteNotebook()
{
    int curIndex = currentIndex();
    VNotebook *notebook = m_notebooks[curIndex];
    QString curName = notebook->getName();
    QString curPath = notebook->getPath();

    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                  QString("Are you sure to delete notebook %1?").arg(curName),
                                  QString("This will delete any files in this notebook (%1).").arg(curPath),
                                  QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok, this);
    if (ret == QMessageBox::Ok) {
        m_editArea->closeFile(notebook, true);
        deleteNotebook(notebook);
    }
}

void VNotebookSelector::deleteNotebook(VNotebook *p_notebook)
{
    int idx = indexOfNotebook(p_notebook);

    m_notebooks.remove(idx);
    vconfig.setNotebooks(m_notebooks);

    removeItem(idx);

    VNotebook::deleteNotebook(p_notebook);
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
    int curIndex = currentIndex();
    Q_ASSERT(curIndex > -1);
    VNotebook *notebook = m_notebooks[curIndex];
    QString info;
    QString curName = notebook->getName();
    QString defaultPath = notebook->getPath();
    QString defaultName(curName);
    do {
        VNotebookInfoDialog dialog(tr("Notebook information"), info, defaultName,
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
            setItemText(curIndex, name);
            vconfig.setNotebooks(m_notebooks);
            emit notebookUpdated(notebook);
        }
        break;
    } while (true);
}
