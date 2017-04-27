#include <QtWidgets>
#include <QDir>
#include "vnewnotebookdialog.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VNewNotebookDialog::VNewNotebookDialog(const QString &title, const QString &info,
                                       const QString &defaultName, const QString &defaultPath,
                                       QWidget *parent)
    : QDialog(parent), infoLabel(NULL),
      title(title), info(info), defaultName(defaultName), defaultPath(defaultPath)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::enableOkButton);
    connect(pathEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::handlePathChanged);
    connect(browseBtn, &QPushButton::clicked, this, &VNewNotebookDialog::handleBrowseBtnClicked);

    enableOkButton();
}

void VNewNotebookDialog::setupUI()
{
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
        infoLabel->setWordWrap(true);
    }
    nameLabel = new QLabel(tr("Notebook &name:"));
    nameEdit = new QLineEdit(defaultName);
    nameLabel->setBuddy(nameEdit);

    QLabel *pathLabel = new QLabel(tr("Notebook &root folder:"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));

    importCheck = new QCheckBox(tr("Try to import existing notebook"), this);
    importCheck->setChecked(true);
    importCheck->setToolTip(tr("When checked, VNote won't create a new config file if there already exists one"));

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(nameLabel, 0, 0);
    topLayout->addWidget(nameEdit, 0, 1, 1, 2);
    topLayout->addWidget(pathLabel, 1, 0);
    topLayout->addWidget(pathEdit, 1, 1);
    topLayout->addWidget(browseBtn, 1, 2);
    topLayout->addWidget(importCheck, 2, 1);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Warning label.
    m_warnLabel = new QLabel(tr("<span style=\"%1\">WARNING</span>: The folder you choose is NOT empty! "
                                "It is highly recommended to use an EMPTY and EXCLUSIVE folder for a notebook. "
                                "Ignore this warning if you do want to import an existing VNote notebook folder.")
                               .arg(vconfig.c_warningTextStyle),
                             this);
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);
}

void VNewNotebookDialog::enableOkButton()
{
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(!pathEdit->text().isEmpty()
                      && !nameEdit->text().isEmpty()
                      && QDir(pathEdit->text()).exists());
}

QString VNewNotebookDialog::getNameInput() const
{
    return nameEdit->text();
}

QString VNewNotebookDialog::getPathInput() const
{
    return pathEdit->text();
}

void VNewNotebookDialog::handleBrowseBtnClicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Root Folder Of The Notebook"),
                                                        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    pathEdit->setText(dirPath);
}

bool VNewNotebookDialog::getImportCheck() const
{
    return importCheck->isChecked();
}

void VNewNotebookDialog::showEvent(QShowEvent *event)
{
    nameEdit->setFocus();
    QDialog::showEvent(event);
}

void VNewNotebookDialog::handlePathChanged(const QString &p_text)
{
    if (!p_text.isEmpty()) {
        QDir dir(p_text);
        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden);
        if (dir.exists() && !files.isEmpty()) {
            m_warnLabel->show();
        } else {
            m_warnLabel->hide();
        }
    } else {
        m_warnLabel->hide();
    }
    enableOkButton();
}
