#include <QtWidgets>
#include "vdirinfodialog.h"
#include "vdirectory.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VDirInfoDialog::VDirInfoDialog(const QString &title,
                               const QString &info,
                               const VDirectory *directory,
                               VDirectory *parentDirectory,
                               QWidget *parent)
    : QDialog(parent), title(title), info(info),
      m_directory(directory), m_parentDirectory(parentDirectory)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VDirInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VDirInfoDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }

    nameEdit = new QLineEdit(m_directory->getName());
    nameEdit->selectAll();

    // Created time.
    QString createdTimeStr = VUtils::displayDateTime(m_directory->getCreatedTimeUtc().toLocalTime());
    QLabel *createdTimeLabel = new QLabel(createdTimeStr);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Folder &name:"), nameEdit);
    topLayout->addRow(tr("Created time:"), createdTimeLabel);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

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

void VDirInfoDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = nameEdit->text();
    bool nameOk = !name.isEmpty();
    if (nameOk && name != m_directory->getName()) {
        // Check if the name conflicts with existing directory name.
        // Case-insensitive when creating note.
        const VDirectory *directory = m_parentDirectory->findSubDirectory(name, false);
        if (directory && directory != m_directory) {
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

QString VDirInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
