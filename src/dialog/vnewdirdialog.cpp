#include <QtWidgets>
#include "vnewdirdialog.h"

VNewDirDialog::VNewDirDialog(const QString &title, const QString &name, const QString &defaultName,
                             const QString &description, const QString &defaultDescription,
                             QWidget *parent)
    : QDialog(parent), title(title), name(name), defaultName(defaultName),
      description(description), defaultDescription(defaultDescription)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewDirDialog::enableOkButton);
    connect(okBtn, &QPushButton::clicked, this, &VNewDirDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VNewDirDialog::reject);
}

void VNewDirDialog::setupUI()
{
    nameLabel = new QLabel(name);
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    descriptionLabel = new QLabel(description);
    descriptionEdit = new QLineEdit(defaultDescription);
    descriptionLabel->setBuddy(descriptionEdit);

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));

    QVBoxLayout *topLayout = new QVBoxLayout();
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(nameEdit);
    topLayout->addWidget(descriptionLabel);
    topLayout->addWidget(descriptionEdit);

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

void VNewDirDialog::enableOkButton(const QString &editText)
{
    okBtn->setEnabled(!editText.isEmpty());
}

QString VNewDirDialog::getNameInput() const
{
    return nameEdit->text();
}

QString VNewDirDialog::getDescriptionInput() const
{
    return descriptionEdit->text();
}
