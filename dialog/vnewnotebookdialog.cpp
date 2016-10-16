#include <QtWidgets>
#include "vnewnotebookdialog.h"

VNewNotebookDialog::VNewNotebookDialog(const QString &title, const QString &info,
                                       const QString &defaultName, const QString &defaultPath,
                                       QWidget *parent)
    : QDialog(parent), title(title), info(info), defaultName(defaultName), defaultPath(defaultPath),
      infoLabel(NULL)
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
    nameLabel = new QLabel(tr("&Name"));
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    QLabel *pathLabel = new QLabel(tr("&Path"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));

    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(pathEdit);
    pathLayout->addWidget(browseBtn);

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));

    QVBoxLayout *topLayout = new QVBoxLayout();
    if (infoLabel) {
        topLayout->addWidget(infoLabel);
    }
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(nameEdit);
    topLayout->addWidget(pathLabel);
    topLayout->addLayout(pathLayout);

    QHBoxLayout *btmLayout = new QHBoxLayout();
    btmLayout->addStretch();
    btmLayout->addWidget(okBtn);
    btmLayout->addWidget(cancelBtn);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(btmLayout);
    setLayout(mainLayout);

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
                                                        "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    pathEdit->setText(dirPath);
}
