#include <QtWidgets>
#include "vdirinfodialog.h"

VDirInfoDialog::VDirInfoDialog(const QString &title, const QString &info,
                               const QString &defaultName,
                               QWidget *parent)
    : QDialog(parent), infoLabel(NULL), title(title), info(info), defaultName(defaultName)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VDirInfoDialog::enableOkButton);
    connect(okBtn, &QPushButton::clicked, this, &VDirInfoDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VDirInfoDialog::reject);

    enableOkButton();
}

void VDirInfoDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }
    nameLabel = new QLabel(tr("&Name:"));
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(nameEdit);

    QHBoxLayout *btmLayout = new QHBoxLayout();
    btmLayout->addStretch();
    btmLayout->addWidget(okBtn);
    btmLayout->addWidget(cancelBtn);

    nameEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(btmLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);

    setWindowTitle(title);
}

void VDirInfoDialog::enableOkButton()
{
    okBtn->setEnabled(!nameEdit->text().isEmpty());
}

QString VDirInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
