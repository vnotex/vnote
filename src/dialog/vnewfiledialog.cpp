#include <QtWidgets>
#include "vnewfiledialog.h"

VNewFileDialog::VNewFileDialog(const QString &title, const QString &info, const QString &name,
                               const QString &defaultName, QWidget *parent)
    : QDialog(parent), title(title), info(info), name(name), defaultName(defaultName)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewFileDialog::enableOkButton);
}

void VNewFileDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }

    nameLabel = new QLabel(name);
    nameEdit = new QLineEdit(defaultName);
    int dotIndex = defaultName.lastIndexOf('.');
    nameEdit->setSelection(0, (dotIndex == -1) ? defaultName.size() : dotIndex);
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

void VNewFileDialog::enableOkButton(const QString &editText)
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!editText.isEmpty());
}

QString VNewFileDialog::getNameInput() const
{
    return nameEdit->text();
}
