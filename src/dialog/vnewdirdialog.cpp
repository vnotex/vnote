#include <QtWidgets>
#include "vnewdirdialog.h"
#include "vdirectory.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VNewDirDialog::VNewDirDialog(const QString &title,
                             const QString &info,
                             const QString &defaultName,
                             VDirectory *directory,
                             QWidget *parent)
    : QDialog(parent), title(title), info(info), defaultName(defaultName),
      m_directory(directory)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewDirDialog::handleInputChanged);

    handleInputChanged();
}

void VNewDirDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
        infoLabel->setWordWrap(true);
    }

    QLabel *nameLabel = new QLabel("Folder &name:");
    nameEdit = new QLineEdit(defaultName);
    nameEdit->selectAll();
    nameLabel->setBuddy(nameEdit);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

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
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);
    setWindowTitle(title);
}

void VNewDirDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = nameEdit->text();
    bool nameOk = !name.isEmpty();
    if (nameOk) {
        // Check if the name conflicts with existing directory name.
        // Case-insensitive when creating folder.
        if (m_directory->findSubDirectory(name, false)) {
            nameOk = false;
            showWarnLabel = true;
            QString nameConflictText = tr("<span style=\"%1\">WARNING</span>: Name (case-insensitive) already exists. "
                                          "Please choose another name.")
                                          .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(nameConflictText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk);
}

QString VNewDirDialog::getNameInput() const
{
    return nameEdit->text();
}
