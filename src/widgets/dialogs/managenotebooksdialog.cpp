#include "managenotebooksdialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QListWidgetItem>
#include <QMenu>
#include <QTimer>
#include <QPushButton>

#include "notebook/notebook.h"
#include "notebookinfowidget.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "../messageboxhelper.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include "../widgetsfactory.h"
#include "exception.h"
#include "../propertydefs.h"
#include "../listwidget.h"

using namespace vnotex;

ManageNotebooksDialog::ManageNotebooksDialog(const Notebook *p_notebook, QWidget *p_parent)
    : Dialog(p_parent)
{
    setupUI();

    loadNotebooks(p_notebook);
}

void ManageNotebooksDialog::setupUI()
{
    auto widget = new QWidget(this);
    setCentralWidget(widget);

    auto mainLayout = new QHBoxLayout(widget);

    setupNotebookList(widget);
    mainLayout->addWidget(m_notebookList);
    mainLayout->setStretchFactor(m_notebookList, 1);

    {
        m_infoScrollArea = new QScrollArea(widget);
        m_infoScrollArea->setWidgetResizable(true);
        mainLayout->addWidget(m_infoScrollArea);
        mainLayout->setStretchFactor(m_infoScrollArea, 3);

        auto infoWidget = new QWidget(widget);
        m_infoScrollArea->setWidget(infoWidget);

        auto infoLayout = new QVBoxLayout(infoWidget);

        setupNotebookInfoWidget(infoWidget);
        infoLayout->addWidget(m_notebookInfoWidget);

        auto btnLayout = new QHBoxLayout();
        infoLayout->addLayout(btnLayout);

        m_closeNotebookBtn = new QPushButton(tr("Close"), infoWidget);
        btnLayout->addStretch();
        btnLayout->addWidget(m_closeNotebookBtn);
        connect(m_closeNotebookBtn, &QPushButton::clicked,
                this, [this]() {
                    if (checkUnsavedChanges()) {
                        return;
                    }
                    closeNotebook(m_notebookInfoWidget->getNotebook());
                });

        m_deleteNotebookBtn = new QPushButton(tr("Delete"), infoWidget);
        WidgetUtils::setPropertyDynamically(m_deleteNotebookBtn, PropertyDefs::s_dangerButton, true);
        btnLayout->addWidget(m_deleteNotebookBtn);
        connect(m_deleteNotebookBtn, &QPushButton::clicked,
                this, [this]() {
                    if (checkUnsavedChanges()) {
                        return;
                    }
                    removeNotebook(m_notebookInfoWidget->getNotebook());
                });
    }

    setDialogButtonBox(QDialogButtonBox::Ok
                       | QDialogButtonBox::Apply
                       | QDialogButtonBox::Reset
                       | QDialogButtonBox::Cancel);

    setWindowTitle(tr("Manage Notebooks"));
}

Notebook *ManageNotebooksDialog::getNotebookFromItem(const QListWidgetItem *p_item) const
{
    Notebook *notebook = nullptr;
    if (p_item) {
        auto id = static_cast<ID>(p_item->data(Qt::UserRole).toULongLong());
        notebook = VNoteX::getInst().getNotebookMgr().findNotebookById(id).data();
    }

    return notebook;
}

void ManageNotebooksDialog::setupNotebookList(QWidget *p_parent)
{
    m_notebookList = new ListWidget(p_parent);
    connect(m_notebookList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *p_item, QListWidgetItem *p_previous) {
                auto notebook = getNotebookFromItem(p_item);
                if (m_changesUnsaved) {
                    // Have unsaved changes.
                    if (m_notebook != notebook) {
                        checkUnsavedChanges();

                        QTimer::singleShot(50, this, [this, p_previous]() {
                            m_notebookList->setCurrentItem(p_previous);
                        });
                    }

                    return;
                }

                m_notebook = notebook;
                selectNotebook(m_notebook);
            });
}

void ManageNotebooksDialog::setupNotebookInfoWidget(QWidget *p_parent)
{
    m_notebookInfoWidget = new NotebookInfoWidget(NotebookInfoWidget::Edit, p_parent);
    connect(m_notebookInfoWidget, &NotebookInfoWidget::basicInfoEdited,
            this, [this]() {
                bool unsaved = false;
                if (m_notebook) {
                    if (m_notebook->getName() != m_notebookInfoWidget->getName()
                        || m_notebook->getDescription() != m_notebookInfoWidget->getDescription()) {
                        unsaved = true;
                    }
                }

                setChangesUnsaved(unsaved);
            });
}

void ManageNotebooksDialog::loadNotebooks(const Notebook *p_notebook)
{
    setChangesUnsaved(false);

    m_notebookList->clear();

    bool hasCurrentItem = false;
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    const auto &notebooks = notebookMgr.getNotebooks();
    for (auto &nb : notebooks) {
        auto item = new QListWidgetItem(nb->getName());
        item->setData(Qt::UserRole, nb->getId());
        item->setToolTip(nb->getName());
        m_notebookList->addItem(item);

        if (nb.data() == p_notebook) {
            hasCurrentItem = true;
            m_notebookList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
        }
    }

    if (!hasCurrentItem && !notebooks.isEmpty()) {
        m_notebookList->setCurrentRow(0);
    }
}

void ManageNotebooksDialog::selectNotebook(Notebook *p_notebook)
{
    m_notebookInfoWidget->setNotebook(p_notebook);
    setChangesUnsaved(false);

    WidgetUtils::resizeToHideScrollBarLater(m_infoScrollArea, false, true);
}

void ManageNotebooksDialog::acceptedButtonClicked()
{
    if (saveChangesToNotebook()) {
        accept();
    }
}

void ManageNotebooksDialog::resetButtonClicked()
{
    m_notebookInfoWidget->clear();
    selectNotebook(m_notebook);
}

void ManageNotebooksDialog::appliedButtonClicked()
{
    Q_ASSERT(m_changesUnsaved);
    if (saveChangesToNotebook()) {
        loadNotebooks(m_notebook);
    }
}

void ManageNotebooksDialog::setChangesUnsaved(bool p_unsaved)
{
    m_changesUnsaved = p_unsaved;
    setButtonEnabled(QDialogButtonBox::Apply, m_changesUnsaved);
    setButtonEnabled(QDialogButtonBox::Reset, m_changesUnsaved);
}

bool ManageNotebooksDialog::saveChangesToNotebook()
{
    if (!m_changesUnsaved || !m_notebook) {
        return true;
    }

    m_notebook->updateName(m_notebookInfoWidget->getName());
    m_notebook->updateDescription(m_notebookInfoWidget->getDescription());
    return true;
}

void ManageNotebooksDialog::closeNotebook(const Notebook *p_notebook)
{
    Q_ASSERT(p_notebook);
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question,
                                                 tr("Close notebook %1?")
                                                   .arg(p_notebook->getName()),
                                                 tr("The notebook could be imported again later."),
                                                 tr("Notebook location: %1").arg(p_notebook->getRootFolderAbsolutePath()),
                                                 this);
    if (ret != QMessageBox::Ok) {
        return;
    }

    try
    {
        VNoteX::getInst().getNotebookMgr().closeNotebook(p_notebook->getId());
    } catch (Exception &p_e) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Failed to close notebook (%1)").arg(p_e.what()),
                                 this);
        loadNotebooks(nullptr);
        return;
    }

    loadNotebooks(nullptr);
}

void ManageNotebooksDialog::removeNotebook(const Notebook *p_notebook)
{
    Q_ASSERT(p_notebook);
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Warning,
                                                 tr("Delete notebook %1 from disk?").arg(p_notebook->getName()),
                                                 tr("It will delete all files belonging to this notebook from disk. "
                                                    "It is dangerous since it will bypass system's recycle bin!"),
                                                 tr("Notebook location: %1").arg(p_notebook->getRootFolderAbsolutePath()),
                                                 this);
    if (ret != QMessageBox::Ok) {
        return;
    }

    try {
        VNoteX::getInst().getNotebookMgr().removeNotebook(p_notebook->getId());
    } catch (Exception &p_e) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Failed to delete notebook (%1)").arg(p_e.what()),
                                 this);
        loadNotebooks(nullptr);
        return;
    }

    loadNotebooks(nullptr);
}

bool ManageNotebooksDialog::checkUnsavedChanges()
{
    if (m_changesUnsaved) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("There are unsaved changes to current notebook."),
                                 this);
        return true;
    }

    return false;
}
