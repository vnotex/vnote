#include <QtWidgets>
#include <QDir>
#include "vnewnotebookdialog.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

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
    checkRootFolder(pathEdit->text());
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

    importCheck = new QCheckBox(tr("Import existing notebook"));
    importCheck->setToolTip(tr("When checked, VNote will read the configuration file to import an existing notebook"));
    connect(importCheck, &QCheckBox::stateChanged,
            this, &VNewNotebookDialog::importCheckChanged);

    QLabel *imageFolderLabel = new QLabel(tr("&Image folder:"));
    m_imageFolderEdit = new QLineEdit();
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(vconfig.getImageFolder()));
    imageFolderLabel->setBuddy(m_imageFolderEdit);
    QString imageFolderTip = tr("Set the name of the folder for all the notes of this notebook to store images "
                                "(empty to use global configuration)");
    m_imageFolderEdit->setToolTip(imageFolderTip);
    imageFolderLabel->setToolTip(imageFolderTip);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_imageFolderEdit);
    m_imageFolderEdit->setValidator(validator);

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(nameLabel, 0, 0);
    topLayout->addWidget(nameEdit, 0, 1, 1, 2);
    topLayout->addWidget(pathLabel, 1, 0);
    topLayout->addWidget(pathEdit, 1, 1);
    topLayout->addWidget(browseBtn, 1, 2);
    topLayout->addWidget(importCheck, 2, 1);
    topLayout->addWidget(imageFolderLabel, 3, 0);
    topLayout->addWidget(m_imageFolderEdit, 3, 1);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    // Warning label.
    m_warnLabel = new QLabel(tr("<span style=\"%1\">WARNING</span>: The folder you choose is NOT empty! "
                                "It is highly recommended to use an EMPTY and EXCLUSIVE folder for a notebook. "
                                "Ignore this warning if you do want to import an existing VNote notebook folder.")
                               .arg(vconfig.c_warningTextStyle));
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    // Will set the parent of above widgets properly.
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

QString VNewNotebookDialog::getImageFolder() const
{
    return m_imageFolderEdit->text();
}

void VNewNotebookDialog::handleBrowseBtnClicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Root Folder Of The Notebook"),
                                                        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        pathEdit->setText(dirPath);
    }
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
    enableOkButton();
    checkRootFolder(p_text);
}

void VNewNotebookDialog::importCheckChanged(int p_state)
{
    // If import existing notebook, disable setting new configs.
    bool checked = p_state == Qt::Checked;

    m_imageFolderEdit->setEnabled(!checked);
}

void VNewNotebookDialog::checkRootFolder(const QString &p_path)
{
    bool existConfig = false;

    if (!p_path.isEmpty()) {
        QDir dir(p_path);
        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden);
        if (!files.isEmpty()) {
            m_warnLabel->show();
        } else {
            m_warnLabel->hide();
        }

        existConfig = VConfigManager::directoryConfigExist(p_path);
    } else {
        m_warnLabel->hide();
    }

    importCheck->setChecked(existConfig);
    importCheck->setEnabled(existConfig);
}
