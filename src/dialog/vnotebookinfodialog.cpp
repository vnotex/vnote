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
    nameLabel = new QLabel(tr("&Name"));
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    QLabel *pathLabel = new QLabel(tr("&Path"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    pathEdit->setEnabled(false);

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
    topLayout->addWidget(pathEdit);

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

void VNotebookInfoDialog::enableOkButton()
{
    okBtn->setEnabled(!nameEdit->text().isEmpty());
}

QString VNotebookInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
