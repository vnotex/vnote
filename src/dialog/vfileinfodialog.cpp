#include <QtWidgets>
#include "vfileinfodialog.h"
#include "vdirectory.h"
#include "vfile.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VFileInfoDialog::VFileInfoDialog(const QString &title, const QString &info,
                                 VDirectory *directory, const VFile *file,
                                 QWidget *parent)
    : QDialog(parent), infoLabel(NULL), title(title), info(info),
      m_directory(directory), m_file(file)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VFileInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VFileInfoDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }
    nameLabel = new QLabel(tr("Note &name:"));
    nameEdit = new QLineEdit(m_file->getName());
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

void VFileInfoDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = nameEdit->text();
    bool nameOk = !name.isEmpty();
    if (nameOk && name != m_file->getName()) {
        // Check if the name conflicts with existing notebook name.
        // Case-insensitive when creating note.
        if (m_directory->findFile(name, false)) {
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

QString VFileInfoDialog::getNameInput() const
{
    return nameEdit->text();
}
