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
    connect(okBtn, &QPushButton::clicked, this, &VNewNotebookDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VNewNotebookDialog::reject);

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

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));
    QHBoxLayout *btmLayout = new QHBoxLayout();
    btmLayout->addStretch();
    btmLayout->addWidget(okBtn);
    btmLayout->addWidget(cancelBtn);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(btmLayout);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);
}

void VNewNotebookDialog::enableOkButton()
{
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
