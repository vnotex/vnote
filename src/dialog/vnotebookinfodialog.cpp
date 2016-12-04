#include <QtWidgets>
#include "vnotebookinfodialog.h"

VNotebookInfoDialog::VNotebookInfoDialog(const QString &title, const QString &info,
                                         const QString &defaultName, const QString &defaultPath,
                                         QWidget *parent)
    : QDialog(parent), infoLabel(NULL), title(title), info(info), defaultName(defaultName),
      defaultPath(defaultPath)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNotebookInfoDialog::enableOkButton);
    connect(okBtn, &QPushButton::clicked, this, &VNotebookInfoDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VNotebookInfoDialog::reject);

    enableOkButton();
}

void VNotebookInfoDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }
    nameLabel = new QLabel(tr("Notebook &name:"));
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);
    QHBoxLayout *nameLayout = new QHBoxLayout();
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit);

    QLabel *pathLabel = new QLabel(tr("Notebook &path:"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    pathEdit->setReadOnly(true);
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(pathEdit);

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
    mainLayout->addLayout(nameLayout);
    mainLayout->addLayout(pathLayout);
    mainLayout->addLayout(btmLayout);
    setLayout(mainLayout);

    setWindowTitle(title);
}

void VNotebookInfoDialog::enableOkButton()
{
    okBtn->setEnabled(!nameEdit->text().isEmpty());
}

QString VNotebookInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
