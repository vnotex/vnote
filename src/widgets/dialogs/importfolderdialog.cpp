#include "importfolderdialog.h"

#include <QLineEdit>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QLabel>

#include "folderfilesfilterwidget.h"
#include "vnotex.h"
#include "exception.h"
#include <utils/pathutils.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include "importfolderutils.h"

using namespace vnotex;

ImportFolderDialog::ImportFolderDialog(Node *p_node, QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_parentNode(p_node)
{
    setupUI();

    m_filterWidget->getFolderPathEdit()->setFocus();
}

void ImportFolderDialog::setupUI()
{
    auto widget = new QWidget(this);
    auto mainLayout = new QVBoxLayout(widget);
    setCentralWidget(widget);

    auto label = new QLabel(tr("Import folder into (%1).").arg(m_parentNode->fetchAbsolutePath()), widget);
    label->setWordWrap(true);
    mainLayout->addWidget(label);

    setupFolderFilesFilterWidget(widget);
    mainLayout->addWidget(m_filterWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    setButtonEnabled(QDialogButtonBox::Ok, false);

    setWindowTitle(tr("Import Folder"));
}

void ImportFolderDialog::setupFolderFilesFilterWidget(QWidget *p_parent)
{
    m_filterWidget = new FolderFilesFilterWidget(p_parent);
    connect(m_filterWidget, &FolderFilesFilterWidget::filesChanged,
            this, [this]() {
                validateInputs();
            });
}

const QSharedPointer<Node> &ImportFolderDialog::getNewNode() const
{
    return m_newNode;
}

void ImportFolderDialog::acceptedButtonClicked()
{
    if (isCompleted() || importFolder()) {
        accept();
    }
}

void ImportFolderDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    auto folder = m_filterWidget->getFolderPath();
    if (!QFileInfo::exists(folder) || !PathUtils::isLegalPath(folder)) {
        msg = tr("Please specify a valid folder to import.");
        valid = false;
    }

    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, valid);
}

bool ImportFolderDialog::importFolder()
{
    const auto folder = m_filterWidget->getFolderPath();
    auto nb = m_parentNode->getNotebook();
    m_newNode = nullptr;
    try {
        m_newNode = nb->copyAsNode(m_parentNode, Node::Flag::Container, folder);
    } catch (Exception &p_e) {
        auto msg = tr("Failed to add folder (%1) as node (%2).").arg(folder, p_e.what());
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    QString errMsg;
    ImportFolderUtils::importFolderContents(nb,
                                            m_newNode.data(),
                                            m_filterWidget->getSuffixes(),
                                            errMsg);

    emit nb->nodeUpdated(m_parentNode);

    if (!errMsg.isEmpty()) {
        qWarning() << errMsg;
        setInformationText(errMsg, ScrollDialog::InformationLevel::Error);
        completeButStay();
        return false;
    }

    return true;
}
