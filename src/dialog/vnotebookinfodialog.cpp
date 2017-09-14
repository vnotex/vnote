#include <QtWidgets>
#include "vnotebookinfodialog.h"
#include "vnotebook.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VNotebookInfoDialog::VNotebookInfoDialog(const QString &p_title,
                                         const QString &p_info,
                                         const VNotebook *p_notebook,
                                         const QVector<VNotebook *> &p_notebooks,
                                         QWidget *p_parent)
    : QDialog(p_parent), m_notebook(p_notebook),
      m_notebooks(p_notebooks)
{
    setupUI(p_title, p_info);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &VNotebookInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VNotebookInfoDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
    }

    m_nameEdit = new QLineEdit(m_notebook->getName());
    m_nameEdit->selectAll();

    m_pathEdit = new QLineEdit(m_notebook->getPath());
    m_pathEdit->setReadOnly(true);

    m_imageFolderEdit = new QLineEdit(m_notebook->getImageFolderConfig());
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(g_config->getImageFolder()));
    m_imageFolderEdit->setToolTip(tr("Set the name of the folder for all the notes of this notebook to store images "
                                     "(empty to use global configuration)"));
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_imageFolderEdit);
    m_imageFolderEdit->setValidator(validator);

    // Recycle bin folder.
    QLineEdit *recycleBinFolderEdit = new QLineEdit(m_notebook->getRecycleBinFolder());
    recycleBinFolderEdit->setReadOnly(true);
    recycleBinFolderEdit->setToolTip(tr("The folder to hold deleted files from within VNote"));

    // Created time.
    QString createdTimeStr = const_cast<VNotebook *>(m_notebook)->getCreatedTimeUtc().toLocalTime()
                                                                                     .toString(Qt::DefaultLocaleLongDate);
    QLabel *createdTimeLabel = new QLabel(createdTimeStr);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Notebook &name:"), m_nameEdit);
    topLayout->addRow(tr("Notebook &root folder:"), m_pathEdit);
    topLayout->addRow(tr("&Image folder:"), m_imageFolderEdit);
    topLayout->addRow(tr("Recycle bin folder:"), recycleBinFolderEdit);
    topLayout->addRow(tr("Created time:"), createdTimeLabel);

    // Warning label.
    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);
}

void VNotebookInfoDialog::handleInputChanged()
{
    QString name = m_nameEdit->text();
    bool nameOk = !name.isEmpty();
    bool showWarnLabel = false;

    if (nameOk && name != m_notebook->getName()) {
        // Check if the name conflicts with existing notebook name.
        // Case-insensitive.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (m_notebooks[idx]->getName().toLower() == name.toLower()) {
                break;
            }
        }

        if (idx < m_notebooks.size() && m_notebooks[idx] != m_notebook) {
            nameOk = false;
            showWarnLabel = true;
            QString nameConflictText = tr("<span style=\"%1\">WARNING</span>: Name (case-insensitive) already exists. "
                                          "Please choose another name.")
                                          .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(nameConflictText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk);
}

QString VNotebookInfoDialog::getName() const
{
    return m_nameEdit->text();
}

QString VNotebookInfoDialog::getImageFolder() const
{
    return m_imageFolderEdit->text();
}

void VNotebookInfoDialog::showEvent(QShowEvent *p_event)
{
    m_nameEdit->setFocus();
    QDialog::showEvent(p_event);
}

