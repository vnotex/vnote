#include <QtWidgets>
#include "vnewdirdialog.h"
#include "vdirectory.h"
#include "vconfigmanager.h"
#include "vmetawordlineedit.h"
#include "utils/vutils.h"

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

    connect(m_nameEdit, &QLineEdit::textChanged, this, &VNewDirDialog::handleInputChanged);

    handleInputChanged();
}

void VNewDirDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
        infoLabel->setWordWrap(true);
    }

    QLabel *nameLabel = new QLabel(tr("Folder &name:"));
    m_nameEdit = new VMetaWordLineEdit(defaultName);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    m_nameEdit->selectAll();
    nameLabel->setBuddy(m_nameEdit);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_btnBox->button(QDialogButtonBox::Ok)->setProperty("SpecialBtn", true);

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(nameLabel);
    topLayout->addWidget(m_nameEdit);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_nameEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

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
    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (nameOk) {
        // Check if the name conflicts with existing directory name.
        // Case-insensitive when creating folder.
        QString warnText;
        if (m_directory->findSubDirectory(name, false)) {
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name (case-insensitive) <span style=\"%2\">%3</span> already exists. "
                          "Please choose another name.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        } else if (!VUtils::checkFileNameLegal(name)) {
            // Check if evaluated name contains illegal characters.
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name <span style=\"%2\">%3</span> contains illegal characters "
                          "(after magic word evaluation).")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        }

        if (!nameOk) {
            showWarnLabel = true;
            m_warnLabel->setText(warnText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk);
}

QString VNewDirDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}
