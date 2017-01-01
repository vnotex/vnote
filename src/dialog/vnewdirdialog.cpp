#include <QtWidgets>
#include "vnewdirdialog.h"

VNewDirDialog::VNewDirDialog(const QString &title, const QString &info, const QString &name, const QString &defaultName,
                             QWidget *parent)
    : QDialog(parent), title(title), info(info), name(name), defaultName(defaultName)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewDirDialog::enableOkButton);
    connect(okBtn, &QPushButton::clicked, this, &VNewDirDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VNewDirDialog::reject);
}

void VNewDirDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }

    nameLabel = new QLabel(name);
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
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
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
