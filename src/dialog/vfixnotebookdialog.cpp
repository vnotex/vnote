#include "vfixnotebookdialog.h"

#include <QtWidgets>

#include "vconfigmanager.h"
#include "vlineedit.h"
#include "vnotebook.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VFixNotebookDialog::VFixNotebookDialog(const VNotebook *p_notebook,
                                       const QVector<VNotebook *> &p_notebooks,
                                       QWidget *p_parent)
    : QDialog(p_parent), m_notebook(p_notebook), m_notebooks(p_notebooks)
{
    setupUI();

    handleInputChanged();
}

void VFixNotebookDialog::setupUI()
{
    QLabel *infoLabel = new QLabel(tr("VNote could not find the root folder of notebook "
                                      "<span style=\"%1\">%2</span>. Please specify the new path "
                                      "to the root folder if you moved it somewhere, or VNote "
                                      "will just remove this notebook.")
                                     .arg(g_config->c_dataTextStyle)
                                     .arg(m_notebook->getName()), this);
    infoLabel->setWordWrap(true);

    QLabel *nameLabel = new QLabel(m_notebook->getName(), this);

    m_pathEdit = new VLineEdit(m_notebook->getPath(), this);
    connect(m_pathEdit, &VLineEdit::textChanged,
            this, &VFixNotebookDialog::handleInputChanged);

    m_browseBtn = new QPushButton(tr("&Browse"), this);
    connect(m_browseBtn, &QPushButton::clicked,
            this, &VFixNotebookDialog::handleBrowseBtnClicked);

    m_relativePathCB = new QCheckBox(tr("Use relative path"), this);
    m_relativePathCB->setToolTip(tr("Use relative path (to VNote's executable) in configuration file"));
    m_relativePathCB->setChecked(!QDir::isAbsolutePath(m_notebook->getPathInConfig()));
    connect(m_relativePathCB, &QCheckBox::stateChanged,
            this, &VFixNotebookDialog::handleInputChanged);

    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(m_browseBtn);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Notebook name:"), nameLabel);
    topLayout->addRow(tr("Notebook root folder:"), pathLayout);
    topLayout->addRow(m_relativePathCB);

    // Warning label.
    m_warnLabel = new QLabel(this);
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(infoLabel);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(tr("Fix Notebook"));
}

void VFixNotebookDialog::handleBrowseBtnClicked()
{
    static QString defaultPath;

    if (defaultPath.isEmpty()) {
        defaultPath = g_config->getVnoteNotebookFolderPath();
        if (!QFileInfo::exists(defaultPath)) {
            defaultPath = g_config->getDocumentPathOrHomePath();
        }
    }

    QString dirPath = QFileDialog::getExistingDirectory(this,
                                                        tr("Select Root Folder Of The Notebook"),
                                                        defaultPath,
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        if (m_pathEdit->text() == dirPath) {
            handleInputChanged();
        } else {
            m_pathEdit->setText(dirPath);
        }

        defaultPath = VUtils::basePathFromPath(dirPath);
    }
}

void VFixNotebookDialog::handleInputChanged()
{
    QString warnText = tr("<span style=\"%1\">WARNING</span>: The folder chosen is NOT a valid root "
                          "folder of a notebook.")
                          .arg(g_config->c_warningTextStyle);
    bool pathOk = false;

    QString path = m_pathEdit->text();
    if (!path.isEmpty()) {
        if (!QDir::isAbsolutePath(path)) {
            QString tmp = tr("<span style=\"%1\">WARNING</span>: Please specify absolute path.")
                            .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(tmp);
        } else if (VConfigManager::directoryConfigExist(path)) {
            pathOk = true;
        }
    }

    if (pathOk) {
        // Check if this path has been in VNote.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (m_notebooks[idx] != m_notebook
                && VUtils::equalPath(m_notebooks[idx]->getPath(), path)) {
                break;
            }
        }

        if (idx < m_notebooks.size()) {
            pathOk = false;
            QString existText = tr("<span style=\"%1\">WARNING</span>: The folder chosen has already been a root folder "
                                   "of existing notebook <span style=\"%2\">%3</span> in VNote.")
                                   .arg(g_config->c_warningTextStyle)
                                   .arg(g_config->c_dataTextStyle)
                                   .arg(m_notebooks[idx]->getName());
            m_warnLabel->setText(existText);
        }
    } else {
        m_warnLabel->setText(warnText);
    }

    if (pathOk && isUseRelativePath()) {
        if (!VUtils::inSameDrive(QCoreApplication::applicationDirPath(), path)) {
            pathOk = false;
            QString existText = tr("<span style=\"%1\">WARNING</span>: Please choose a folder in the same drive as "
                                   "<span style=\"%2\">%3</span> when relative path is enabled.")
                                   .arg(g_config->c_warningTextStyle)
                                   .arg(g_config->c_dataTextStyle)
                                   .arg(QCoreApplication::applicationDirPath());
            m_warnLabel->setText(existText);
        }
    }

    m_warnLabel->setVisible(!pathOk);
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(pathOk);
}

QString VFixNotebookDialog::getPathInput() const
{
    // absoluteFilePath() to convert the drive to upper case.
    // cleanPath() to remove duplicate separator, '.', and '..'.
    QString ret;
    if (isUseRelativePath()) {
        // Use relative path in config file.
        QDir appDir(QCoreApplication::applicationDirPath());
        ret = QDir::cleanPath(appDir.relativeFilePath(m_pathEdit->text()));
    } else {
        ret = QDir::cleanPath(QFileInfo(m_pathEdit->text()).absoluteFilePath());
    }

    return ret;
}

bool VFixNotebookDialog::isUseRelativePath() const
{
    return m_relativePathCB->isChecked();
}
