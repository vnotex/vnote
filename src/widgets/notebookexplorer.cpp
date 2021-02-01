#include "notebookexplorer.h"

#include <QVBoxLayout>
#include <QFileDialog>

#include "titlebar.h"
#include "dialogs/newnotebookdialog.h"
#include "dialogs/newnotebookfromfolderdialog.h"
#include "dialogs/newfolderdialog.h"
#include "dialogs/newnotedialog.h"
#include "dialogs/managenotebooksdialog.h"
#include "dialogs/importnotebookdialog.h"
#include "dialogs/importfolderdialog.h"
#include "dialogs/importlegacynotebookdialog.h"
#include "vnotex.h"
#include "mainwindow.h"
#include "notebook/notebook.h"
#include "notebookmgr.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include "notebookselector.h"
#include "notebooknodeexplorer.h"
#include "messageboxhelper.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/events.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include "navigationmodemgr.h"

using namespace vnotex;

NotebookExplorer::NotebookExplorer(QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI();
}

void NotebookExplorer::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Title bar.
    auto titleBar = setupTitleBar(this);
    mainLayout->addWidget(titleBar);

    // Selector.
    m_selector = new NotebookSelector(this);
    m_selector->setWhatsThis(tr("Select one of all the notebooks as current notebook.<br/>"
                                "Move mouse on one item to check its details."));
    NavigationModeMgr::getInst().registerNavigationTarget(m_selector);
    connect(m_selector, QOverload<int>::of(&QComboBox::activated),
            this, [this](int p_idx) {
                auto id = static_cast<ID>(m_selector->itemData(p_idx).toULongLong());
                emit notebookActivated(id);
            });
    mainLayout->addWidget(m_selector);

    m_nodeExplorer = new NotebookNodeExplorer(this);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeActivated,
            &VNoteX::getInst(), &VNoteX::openNodeRequested);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeAboutToMove,
            &VNoteX::getInst(), &VNoteX::nodeAboutToMove);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeAboutToRemove,
            &VNoteX::getInst(), &VNoteX::nodeAboutToRemove);
    mainLayout->addWidget(m_nodeExplorer);

    setFocusProxy(m_nodeExplorer);
}

TitleBar *NotebookExplorer::setupTitleBar(QWidget *p_parent)
{
    auto titleBar = new TitleBar(tr("Notebook"),
                                 TitleBar::Action::Menu,
                                 p_parent);
    titleBar->setWhatsThis(tr("This title bar contains buttons and menu to manage notebooks and notes."));

    titleBar->addMenuAction(QStringLiteral("manage_notebooks.svg"),
                            tr("&Manage Notebooks"),
                            titleBar,
                            [this]() {
                                ManageNotebooksDialog dialog(m_currentNotebook.data(),
                                                             VNoteX::getInst().getMainWindow());
                                dialog.exec();
                            });

    return titleBar;
}

void NotebookExplorer::loadNotebooks()
{
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    const auto &notebooks = notebookMgr.getNotebooks();
    m_selector->setNotebooks(notebooks);

    emit updateTitleBarMenuActions();
}

void NotebookExplorer::reloadNotebook(const Notebook *p_notebook)
{
    m_selector->reloadNotebook(p_notebook);
}

void NotebookExplorer::setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    m_currentNotebook = p_notebook;

    ID id = p_notebook ? p_notebook->getId() : Notebook::InvalidId;
    m_selector->setCurrentNotebook(id);

    m_nodeExplorer->setNotebook(p_notebook);

    emit updateTitleBarMenuActions();
}

void NotebookExplorer::newNotebook()
{
    NewNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::importNotebook()
{
    ImportNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::newFolder()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create folder within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    NewFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());
    }
}

void NotebookExplorer::newNote()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create note within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    NewNoteDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());

        // Open it right now.
        auto paras = QSharedPointer<FileOpenParameters>::create();
        paras->m_mode = FileOpenParameters::Mode::Edit;
        paras->m_newFile = true;
        emit VNoteX::getInst().openNodeRequested(dialog.getNewNode().data(), paras);
    }
}

Node *NotebookExplorer::currentExploredFolderNode() const
{
    Q_ASSERT(m_currentNotebook);

    auto node = m_nodeExplorer->getCurrentNode();
    if (node) {
        if (!node->isContainer()) {
            node = node->getParent();
        }
        Q_ASSERT(node && node->isContainer());
    } else {
        node = m_currentNotebook->getRootNode().data();
    }

    return node;
}

Node *NotebookExplorer::checkNotebookAndGetCurrentExploredFolderNode() const
{
    if (!m_currentNotebook) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Please first create a notebook to hold your data."),
                                 VNoteX::getInst().getMainWindow());
        return nullptr;
    }

    auto node = currentExploredFolderNode();
    Q_ASSERT(m_currentNotebook.data() == node->getNotebook());
    return node;
}

void NotebookExplorer::newNotebookFromFolder()
{
    NewNotebookFromFolderDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::importFile()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create note within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    static QString lastFolderPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(VNoteX::getInst().getMainWindow(),
                                                      tr("Select Files To Import"),
                                                      lastFolderPath);
    if (files.isEmpty()) {
        return;
    }

    QString errMsg;
    for (const auto &file : files) {
        try {
            m_currentNotebook->copyAsNode(node, Node::Flag::Content, file);
        } catch (Exception &p_e) {
            errMsg += tr("Failed to add file (%1) as node (%2).\n").arg(file, p_e.what());
        }
    }

    if (!errMsg.isEmpty()) {
        MessageBoxHelper::notify(MessageBoxHelper::Critical, errMsg, VNoteX::getInst().getMainWindow());
    }

    emit m_currentNotebook->nodeUpdated(node);
    m_nodeExplorer->setCurrentNode(node);
}

void NotebookExplorer::importFolder()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create folder within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    ImportFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());
    }
}

void NotebookExplorer::importLegacyNotebook()
{
    ImportLegacyNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::locateNode(Node *p_node)
{
    Q_ASSERT(p_node);
    auto nb = p_node->getNotebook();
    if (nb != m_currentNotebook) {
        emit notebookActivated(nb->getId());
    }
    m_nodeExplorer->setCurrentNode(p_node);
    m_nodeExplorer->setFocus();
}


