#include <QtWidgets>
#include <QDir>
#include "vnewnotebookdialog.h"

VNewNotebookDialog::VNewNotebookDialog(const QString &title, const QString &info,
                                       const QString &defaultName, const QString &defaultPath,
                                       QWidget *parent)
    : QDialog(parent), infoLabel(NULL),
      title(title), info(info), defaultName(defaultName), defaultPath(defaultPath)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::enableOkButton);
    connect(pathEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::enableOkButton);
    connect(browseBtn, &QPushButton::clicked, this, &VNewNotebookDialog::handleBrowseBtnClicked);

    enableOkButton();
}

void VNewNotebookDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }
    nameLabel = new QLabel(tr("Notebook &name:"));
    nameEdit = new QLineEdit(defaultName);
    nameLabel->setBuddy(nameEdit);

    QLabel *pathLabel = new QLabel(tr("Notebook &path:"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));

    importCheck = new QCheckBox(tr("Import existing notebook"), this);
    importCheck->setChecked(true);
    importCheck->setToolTip(tr("When checked, VNote won't create a new config file if there already exists one."));

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(nameLabel, 0, 0);
    topLayout->addWidget(nameEdit, 0, 1, 1, 2);
    topLayout->addWidget(pathLabel, 1, 0);
    topLayout->addWidget(pathEdit, 1, 1);
    topLayout->addWidget(browseBtn, 1, 2);
    topLayout->addWidget(importCheck, 2, 1);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);
}

void VNewNotebookDialog::enableOkButton()
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!pathEdit->text().isEmpty() && !nameEdit->text().isEmpty());
}

QString VNewNotebookDialog::getNameInput() const
{
    return nameEdit->text();
}

QString VNewNotebookDialog::getPathInput() const
{
    return pathEdit->text();
}

void VNewNotebookDialog::handleBrowseBtnClicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select a directory as the path of the notebook"),
                                                        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    pathEdit->setText(dirPath);
}

bool VNewNotebookDialog::getImportCheck() const
{
    return importCheck->isChecked();
}

void VNewNotebookDialog::showEvent(QShowEvent *event)
{
    nameEdit->setFocus();
    QDialog::showEvent(event);
}
