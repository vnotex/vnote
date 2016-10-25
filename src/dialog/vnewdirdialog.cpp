#include <QtWidgets>
#include "vnewdirdialog.h"

VNewDirDialog::VNewDirDialog(const QString &title, const QString &name, const QString &defaultName,
                             QWidget *parent)
    : QDialog(parent), title(title), name(name), defaultName(defaultName)
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

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));

    QVBoxLayout *topLayout = new QVBoxLayout();
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(nameEdit);

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
