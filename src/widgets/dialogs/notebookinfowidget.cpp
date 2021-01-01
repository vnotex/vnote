#include "notebookinfowidget.h"

#include <QtWidgets>

#include "../widgetsfactory.h"
#include "notebook/notebook.h"
#include "vnotex.h"
#include "notebook/inotebookfactory.h"
#include "notebook/notebookparameters.h"
#include "versioncontroller/iversioncontrollerfactory.h"
#include "versioncontroller/iversioncontroller.h"
#include "notebookconfigmgr/inotebookconfigmgrfactory.h"
#include "notebookconfigmgr/inotebookconfigmgr.h"
#include "notebookbackend/inotebookbackendfactory.h"
#include "notebookbackend/inotebookbackend.h"
#include "notebookmgr.h"
#include "configmgr.h"
#include <utils/pathutils.h>
#include "exception.h"
#include <utils/widgetutils.h>

using namespace vnotex;

NotebookInfoWidget::NotebookInfoWidget(NotebookInfoWidget::Mode p_mode, QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(p_mode)
{
    setupUI();

    setMode(p_mode);
}

void NotebookInfoWidget::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    auto basicInfoGroup = setupBasicInfoGroupBox(this);
    mainLayout->addWidget(basicInfoGroup);

    auto advancedInfoGroup = setupAdvancedInfoGroupBox(this);
    mainLayout->addWidget(advancedInfoGroup);
}

QGroupBox *NotebookInfoWidget::setupBasicInfoGroupBox(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("Basic Information"), p_parent);
    auto mainLayout = WidgetUtils::createFormLayout(box);

    {
        setupNotebookTypeComboBox(box);
        mainLayout->addRow(tr("Type:"), m_typeComboBox);
    }

    {
        m_nameLineEdit = WidgetsFactory::createLineEdit(box);
        m_nameLineEdit->setPlaceholderText(tr("Name of notebook"));
        connect(m_nameLineEdit, &QLineEdit::textEdited,
                this, &NotebookInfoWidget::basicInfoEdited);
        mainLayout->addRow(tr("Name:"), m_nameLineEdit);
    }

    {
        mainLayout->addRow(tr("Icon:"), new QLabel("default", box));
    }

    {
        m_descriptionLineEdit = WidgetsFactory::createLineEdit(box);
        m_descriptionLineEdit->setPlaceholderText(tr("Description of notebook"));
        connect(m_descriptionLineEdit, &QLineEdit::textEdited,
                this, &NotebookInfoWidget::basicInfoEdited);
        mainLayout->addRow(tr("Description:"), m_descriptionLineEdit);
    }

    {
        auto layout = setupNotebookRootFolderPath(box);
        mainLayout->addRow(tr("Root folder:"), layout);
    }

    return box;
}

void NotebookInfoWidget::setupNotebookTypeComboBox(QWidget *p_parent)
{
    m_typeComboBox = WidgetsFactory::createComboBox(p_parent);
    m_typeComboBox->setToolTip(tr("Type of notebook"));

    QString whatsThis = tr("Specify the type of notebook.<br/>");
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    for (auto &factory : notebookMgr.getAllNotebookFactories()) {
        m_typeComboBox->addItem(factory->getDisplayName(), factory->getName());
        whatsThis += tr("<b>%1</b>: %2<br/>").arg(factory->getDisplayName(),
                                                  factory->getDescription());
    }

    m_typeComboBox->setWhatsThis(whatsThis);
}

QLayout *NotebookInfoWidget::setupNotebookRootFolderPath(QWidget *p_parent)
{
    m_rootFolderPathLineEdit = WidgetsFactory::createLineEdit(p_parent);
    m_rootFolderPathLineEdit->setPlaceholderText(tr("Path of notebook root folder"));
    auto whatsThis = tr("<b>Notebook Root Folder</b> is the folder containing all data of one notebook in %1.").arg(ConfigMgr::c_appName);
    m_rootFolderPathLineEdit->setWhatsThis(whatsThis);
    connect(m_rootFolderPathLineEdit, &QLineEdit::textChanged,
            this, [this]() {
                emit rootFolderEdited();
                emit basicInfoEdited();
            });

    m_rootFolderPathBrowseButton  = new QPushButton(tr("Browse"), p_parent);
    connect(m_rootFolderPathBrowseButton, &QPushButton::clicked,
            this, [this]() {
                auto &config = ConfigMgr::getInst().getSessionConfig();
                auto browsePath = config.getNewNotebookDefaultRootFolderPath();
                auto rootFolderPath = QFileDialog::getExistingDirectory(this,
                        tr("Select Notebook Root Folder"),
                        browsePath,
                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                if (!rootFolderPath.isEmpty()) {
                    config.setNewNotebookDefaultRootFolderPath(PathUtils::parentDirPath(rootFolderPath));
                    m_rootFolderPathLineEdit->setText(rootFolderPath);
                }
            });

    auto layout = new QHBoxLayout();
    layout->addWidget(m_rootFolderPathLineEdit);
    layout->addWidget(m_rootFolderPathBrowseButton);
    return layout;
}

QGroupBox *NotebookInfoWidget::setupAdvancedInfoGroupBox(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("Advanced Information"), p_parent);
    auto mainLayout = WidgetUtils::createFormLayout(box);

    {
        setupConfigMgrComboBox(box);
        mainLayout->addRow(tr("Configuration manager:"), m_configMgrComboBox);
    }

    {
        setupVersionControllerComboBox(box);
        mainLayout->addRow(tr("Version control:"), m_versionControllerComboBox);
    }

    {
        setupBackendComboBox(box);
        mainLayout->addRow(tr("Backend:"), m_backendComboBox);
    }

    return box;
}

void NotebookInfoWidget::setupConfigMgrComboBox(QWidget *p_parent)
{
    m_configMgrComboBox = WidgetsFactory::createComboBox(p_parent);
    m_configMgrComboBox->setToolTip(tr("Configuration manager of notebook"));

    QString whatsThis = tr("Specify configruation manager of notebook.<br/>");
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    for (auto &factory : notebookMgr.getAllNotebookConfigMgrFactories()) {
        m_configMgrComboBox->addItem(factory->getDisplayName(), factory->getName());
        whatsThis += tr("<b>%1</b>: %2<br/>").arg(factory->getDisplayName(),
                                                  factory->getDescription());
    }

    m_configMgrComboBox->setWhatsThis(whatsThis);
}

void NotebookInfoWidget::setupVersionControllerComboBox(QWidget *p_parent)
{
    m_versionControllerComboBox = WidgetsFactory::createComboBox(p_parent);
    m_versionControllerComboBox->setToolTip(tr("Version control of notebook"));

    QString whatsThis = tr("Specify version control of notebook.<br/>");
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    for (auto &factory : notebookMgr.getAllVersionControllerFactories()) {
        m_versionControllerComboBox->addItem(factory->getDisplayName(), factory->getName());
        whatsThis += tr("<b>%1</b>: %2<br/>").arg(factory->getDisplayName(),
                                                  factory->getDescription());
    }

    m_versionControllerComboBox->setWhatsThis(whatsThis);
}

void NotebookInfoWidget::setupBackendComboBox(QWidget *p_parent)
{
    m_backendComboBox = WidgetsFactory::createComboBox(p_parent);
    m_backendComboBox->setToolTip(tr("Backend of notebook"));

    QString whatsThis = tr("Specify backend of notebook.<br/>");
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    for (auto &factory : notebookMgr.getAllNotebookBackendFactories()) {
        m_backendComboBox->addItem(factory->getDisplayName(), factory->getName());
        whatsThis += tr("<b>%1</b>: %2<br/>").arg(factory->getDisplayName(),
                                                  factory->getDescription());
    }

    m_backendComboBox->setWhatsThis(whatsThis);

    connect(m_backendComboBox, QOverload<int>::of(&QComboBox::activated),
            this, &NotebookInfoWidget::notebookBackendEdited);
}

static void setCurrentComboBoxByData(QComboBox *p_box, const QVariant &p_data)
{
    int idx = p_box->findData(p_data);
    p_box->setCurrentIndex(idx);
}

void NotebookInfoWidget::setNotebook(const Notebook *p_notebook)
{
    Q_ASSERT(m_mode != Mode::Create
             && m_mode != Mode::CreateFromFolder
             && m_mode != Mode::CreateFromLegacy);
    if (m_notebook == p_notebook) {
        return;
    }

    bool isImport = m_mode == Mode::Import;
    clear(isImport, isImport);

    m_notebook = p_notebook;
    if (m_notebook) {
        setCurrentComboBoxByData(m_typeComboBox, m_notebook->getType());
        m_nameLineEdit->setText(m_notebook->getName());
        m_descriptionLineEdit->setText(m_notebook->getDescription());
        m_rootFolderPathLineEdit->setText(m_notebook->getRootFolderPath());
        setCurrentComboBoxByData(m_configMgrComboBox, m_notebook->getConfigMgr()->getName());
        setCurrentComboBoxByData(m_versionControllerComboBox, m_notebook->getVersionController()->getName());
        setCurrentComboBoxByData(m_backendComboBox, m_notebook->getBackend()->getName());
    }
}

const Notebook *NotebookInfoWidget::getNotebook() const
{
    Q_ASSERT(m_mode != Mode::Create
             && m_mode != Mode::CreateFromFolder
             && m_mode != Mode::CreateFromLegacy);
    return m_notebook;
}

QLineEdit *NotebookInfoWidget::getNameLineEdit() const
{
    return m_nameLineEdit;
}

QLineEdit *NotebookInfoWidget::getRootFolderPathLineEdit() const
{
    return m_rootFolderPathLineEdit;
}

QLineEdit *NotebookInfoWidget::getDescriptionLineEdit() const
{
    return m_descriptionLineEdit;
}

QComboBox *NotebookInfoWidget::getTypeComboBox() const
{
    return m_typeComboBox;
}

QComboBox *NotebookInfoWidget::getConfigMgrComboBox() const
{
    return m_configMgrComboBox;
}

QComboBox *NotebookInfoWidget::getVersionControllerComboBox() const
{
    return m_versionControllerComboBox;
}

void NotebookInfoWidget::setStateAccordingToMode()
{
    switch (m_mode) {
    case CreateFromFolder:
        m_rootFolderPathLineEdit->setReadOnly(true);
        m_rootFolderPathBrowseButton->setVisible(false);
        break;

    case Edit:
        m_typeComboBox->setEnabled(false);
        m_rootFolderPathLineEdit->setReadOnly(true);
        m_rootFolderPathBrowseButton->setVisible(false);
        m_configMgrComboBox->setEnabled(false);
        m_versionControllerComboBox->setEnabled(false);
        m_backendComboBox->setEnabled(false);
        break;

    case Import:
        m_typeComboBox->setEnabled(false);
        m_nameLineEdit->setEnabled(false);
        m_descriptionLineEdit->setEnabled(false);
        m_configMgrComboBox->setEnabled(false);
        m_versionControllerComboBox->setEnabled(false);
        break;

    case CreateFromLegacy:
        // Support bundle notebook only.
        m_typeComboBox->setEnabled(false);
        break;

    default:
        break;
    }
}

QComboBox *NotebookInfoWidget::getBackendComboBox() const
{
    return m_backendComboBox;
}

QString NotebookInfoWidget::getName() const
{
    return getNameLineEdit()->text().trimmed();
}

QString NotebookInfoWidget::getDescription() const
{
    return getDescriptionLineEdit()->text().trimmed();
}

QString NotebookInfoWidget::getRootFolderPath() const
{
    return getRootFolderPathLineEdit()->text().trimmed();
}

QIcon NotebookInfoWidget::getIcon() const
{
    return QIcon();
}

QString NotebookInfoWidget::getType() const
{
    return getTypeComboBox()->currentData().toString();
}

QString NotebookInfoWidget::getConfigMgr() const
{
    return getConfigMgrComboBox()->currentData().toString();
}

QString NotebookInfoWidget::getVersionController() const
{
    return getVersionControllerComboBox()->currentData().toString();
}

QString NotebookInfoWidget::getBackend() const
{
    return getBackendComboBox()->currentData().toString();
}

void NotebookInfoWidget::clear(bool p_skipRootFolder, bool p_skipBackend)
{
    Q_UNUSED(p_skipBackend);

    m_notebook = nullptr;
    m_nameLineEdit->clear();
    m_descriptionLineEdit->clear();
    if (!p_skipRootFolder) {
        m_rootFolderPathLineEdit->clear();
    }
}

void NotebookInfoWidget::setRootFolderPath(const QString &p_path)
{
    Q_ASSERT(m_mode == Mode::CreateFromFolder);
    m_rootFolderPathLineEdit->setText(p_path);
    if (m_nameLineEdit->text().isEmpty()) {
        m_nameLineEdit->setText(PathUtils::dirName(p_path));
    }
}

void NotebookInfoWidget::setMode(Mode p_mode)
{
    m_mode = p_mode;
    setStateAccordingToMode();
}
