#include <QtWidgets>
#include <QDir>
#include "vnewnotebookdialog.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vnotebook.h"

extern VConfigManager vconfig;

VNewNotebookDialog::VNewNotebookDialog(const QString &title, const QString &info,
                                       const QString &defaultName, const QString &defaultPath,
                                       const QVector<VNotebook *> &p_notebooks,
                                       QWidget *parent)
    : QDialog(parent), infoLabel(NULL),
      title(title), info(info), defaultName(defaultName), defaultPath(defaultPath),
      m_importNotebook(false), m_manualPath(false), m_manualName(false),
      m_notebooks(p_notebooks)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::handleInputChanged);
    connect(pathEdit, &QLineEdit::textChanged, this, &VNewNotebookDialog::handleInputChanged);
    connect(browseBtn, &QPushButton::clicked, this, &VNewNotebookDialog::handleBrowseBtnClicked);

    handleInputChanged();
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
    topLayout->addWidget(imageFolderLabel, 2, 0);
    topLayout->addWidget(m_imageFolderEdit, 2, 1);

    // Warning label.
    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

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

QString VNewNotebookDialog::getNameInput() const
{
    return nameEdit->text();
}

QString VNewNotebookDialog::getPathInput() const
{
    return QDir::cleanPath(pathEdit->text());
}

QString VNewNotebookDialog::getImageFolder() const
{
    if (m_imageFolderEdit->isEnabled()) {
        return m_imageFolderEdit->text();
    } else {
        return QString();
    }
}

void VNewNotebookDialog::handleBrowseBtnClicked()
{
    static QString defaultPath;
    if (defaultPath.isEmpty()) {
        defaultPath = vconfig.getVnoteNotebookFolderPath();
        if (!QFileInfo::exists(defaultPath)) {
            defaultPath = QDir::homePath();
        }
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Root Folder Of The Notebook"),
                                                        defaultPath,
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        m_manualPath = true;
        pathEdit->setText(dirPath);
        defaultPath = VUtils::basePathFromPath(dirPath);
    }
}

bool VNewNotebookDialog::isImportExistingNotebook() const
{
    return m_importNotebook;
}

void VNewNotebookDialog::showEvent(QShowEvent *event)
{
    nameEdit->setFocus();
    QDialog::showEvent(event);
}

void VNewNotebookDialog::handleInputChanged()
{
    QString warnText = tr("<span style=\"%1\">WARNING</span>: The folder chosen is NOT empty! "
                          "It is highly recommended to use an EMPTY and EXCLUSIVE folder for a new notebook.")
                          .arg(vconfig.c_warningTextStyle);
    QString infoText = tr("<span style=\"%1\">INFO</span>: The folder chosen seems to be a root "
                          "folder of a notebook created by VNote before. "
                          "VNote will try to import it by reading the configuration file.")
                          .arg("font-weight:bold;");
    bool pathOk = false;
    bool configExist = false;
    bool showWarnLabel = false;

    // User has input some texts.
    if (pathEdit->isModified()) {
        m_manualPath = true;
    }

    if (nameEdit->isModified()) {
        m_manualName = true;
    }

    if (autoComplete()) {
        return;
    }

    QString path = pathEdit->text();
    if (!path.isEmpty()) {
        if (QFileInfo::exists(path)) {
            QDir dir(path);
            QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden);
            if (!files.isEmpty()) {
                // Folder is not empty.
                configExist = VConfigManager::directoryConfigExist(path);
                showWarnLabel = true;
            }
        } else {
            pathOk = true;
        }
    }

    if (configExist) {
        pathOk = true;
        m_warnLabel->setText(infoText);
    } else {
        m_warnLabel->setText(warnText);
    }

    // Try to validate if this is a legal path on the OS.
    if (pathOk) {
        pathOk = VUtils::checkPathLegal(path);
        if (!pathOk) {
            showWarnLabel = true;
            QString tmp = tr("<span style=\"%1\">WARNING</span>: The path seems to be illegal. "
                             "Please choose another one.")
                            .arg(vconfig.c_warningTextStyle);
            m_warnLabel->setText(tmp);
        }
    }

    if (pathOk) {
        // Check if this path has been in VNote.
        int idx = -1;
        path = QDir::cleanPath(path);
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (QDir::cleanPath(m_notebooks[idx]->getPath()) == path) {
                break;
            }
        }

        if (idx < m_notebooks.size()) {
            pathOk = false;
            showWarnLabel = true;
            QString existText = tr("<span style=\"%1\">WARNING</span>: The folder chosen has already been a root folder "
                                   "of existing notebook <span style=\"%2\">%3</span> in VNote.")
                                   .arg(vconfig.c_warningTextStyle)
                                   .arg(vconfig.c_dataTextStyle)
                                   .arg(m_notebooks[idx]->getName());
            m_warnLabel->setText(existText);
        }
    }

    QString name = nameEdit->text();
    bool nameOk = !name.isEmpty();
    if (pathOk && nameOk) {
        // Check if the name conflicts with existing notebook name.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (m_notebooks[idx]->getName() == name) {
                break;
            }
        }

        if (idx < m_notebooks.size()) {
            nameOk = false;
            showWarnLabel = true;
            QString nameConflictText = tr("<span style=\"%1\">WARNING</span>: Name already exists. "
                                          "Please choose another name.")
                                          .arg(vconfig.c_warningTextStyle);
            m_warnLabel->setText(nameConflictText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);
    m_importNotebook = configExist;
    m_imageFolderEdit->setEnabled(!m_importNotebook);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk && pathOk);
}

bool VNewNotebookDialog::autoComplete()
{
    if (m_manualPath) {
        return false;
    }

    QString vnoteFolder = vconfig.getVnoteNotebookFolderPath();
    QString pathText = pathEdit->text();
    if (!pathText.isEmpty() && vnoteFolder != VUtils::basePathFromPath(pathText)) {
        return false;
    }

    bool ret = false;
    QString nameText = nameEdit->text();
    if (nameText.isEmpty()) {
        if (m_manualName) {
            return false;
        }

        // Get a folder name under vnoteFolder and set it as the name of the notebook.
        QString name = "vnotebook";
        name = VUtils::getFileNameWithSequence(vnoteFolder, name);
        nameEdit->setText(name);
        ret = true;
    } else {
        // Use the name as the folder name under vnoteFolder.
        QString autoPath = QDir::cleanPath(QDir(vnoteFolder).filePath(nameText));
        if (autoPath != pathText) {
            pathEdit->setText(autoPath);
            ret = true;
        }
    }

    return ret;
}
