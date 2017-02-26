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

    QLabel *pathLabel = new QLabel(tr("Notebook &path:"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    pathEdit->setReadOnly(true);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(nameLabel, nameEdit);
    topLayout->addRow(pathLabel, pathEdit);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);
}

void VNotebookInfoDialog::enableOkButton()
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!nameEdit->text().isEmpty());
}

QString VNotebookInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
