#include <QtWidgets>
#include "vnotebookinfodialog.h"
#include "vnotebook.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VNotebookInfoDialog::VNotebookInfoDialog(const QString &p_title, const QString &p_info,
                                         const VNotebook *p_notebook, QWidget *p_parent)
    : QDialog(p_parent), m_notebook(p_notebook), m_infoLabel(NULL)
{
    setupUI(p_title, p_info);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &VNotebookInfoDialog::enableOkButton);

    enableOkButton();
}

void VNotebookInfoDialog::setupUI(const QString &p_title, const QString &p_info)
{
    if (!p_info.isEmpty()) {
        m_infoLabel = new QLabel(p_info);
    }

    QLabel *nameLabel = new QLabel(tr("Notebook &name:"));
    m_nameEdit = new QLineEdit(m_notebook->getName());
    m_nameEdit->selectAll();
    nameLabel->setBuddy(m_nameEdit);

    QLabel *pathLabel = new QLabel(tr("Notebook &root folder:"));
    m_pathEdit = new QLineEdit(m_notebook->getPath());
    pathLabel->setBuddy(m_pathEdit);
    m_pathEdit->setReadOnly(true);

    QLabel *imageFolderLabel = new QLabel(tr("&Image folder:"));
    m_imageFolderEdit = new QLineEdit(m_notebook->getImageFolderConfig());
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(vconfig.getImageFolder()));
    imageFolderLabel->setBuddy(m_imageFolderEdit);
    QString imageFolderTip = tr("Set the name of the folder for all the notes of this notebook to store images "
                                "(empty to use global configuration)");
    m_imageFolderEdit->setToolTip(imageFolderTip);
    imageFolderLabel->setToolTip(imageFolderTip);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_imageFolderEdit);
    m_imageFolderEdit->setValidator(validator);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(nameLabel, m_nameEdit);
    topLayout->addRow(pathLabel, m_pathEdit);
    topLayout->addRow(imageFolderLabel, m_imageFolderEdit);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (m_infoLabel) {
        mainLayout->addWidget(m_infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);
}

void VNotebookInfoDialog::enableOkButton()
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!m_nameEdit->text().isEmpty());
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

