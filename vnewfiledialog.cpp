#include <QtWidgets>
#include "vnewfiledialog.h"

VNewFileDialog::VNewFileDialog(const QString &title, const QString &name, const QString &defaultName,
                             const QString &description, const QString &defaultDescription,
                             QWidget *parent)
    : QDialog(parent), title(title), name(name), defaultName(defaultName),
      description(description), defaultDescription(defaultDescription)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewFileDialog::enableOkButton);
    connect(okBtn, &QPushButton::clicked, this, &VNewFileDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VNewFileDialog::reject);
}

void VNewFileDialog::setupUI()
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

void VNewFileDialog::enableOkButton(const QString &editText)
{
    okBtn->setEnabled(!editText.isEmpty());
}

QString VNewFileDialog::getNameInput() const
{
    return nameEdit->text();
}

QString VNewFileDialog::getDescriptionInput() const
{
    return descriptionEdit->text();
}
