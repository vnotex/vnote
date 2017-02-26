#include <QtWidgets>
#include "vfileinfodialog.h"

VFileInfoDialog::VFileInfoDialog(const QString &title, const QString &info,
                                 const QString &defaultName,
                                 QWidget *parent)
    : QDialog(parent), infoLabel(NULL), title(title), info(info), defaultName(defaultName)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VFileInfoDialog::enableOkButton);

    enableOkButton();
}

void VFileInfoDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }
    nameLabel = new QLabel(tr("Note &name:"));
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(nameEdit);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    nameEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);

    setWindowTitle(title);
}

void VFileInfoDialog::enableOkButton()
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!nameEdit->text().isEmpty());
}

QString VFileInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
