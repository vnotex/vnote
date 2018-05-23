#include <QtWidgets>
#include <QDir>
#include "vnewnotebookdialog.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vnotebook.h"
#include "vmetawordlineedit.h"

extern VConfigManager *g_config;

VNewNotebookDialog::VNewNotebookDialog(const QString &title, const QString &info,
                                       const QString &defaultName, const QString &defaultPath,
                                       const QVector<VNotebook *> &p_notebooks,
                                       QWidget *parent)
    : QDialog(parent),
      defaultName(defaultName), defaultPath(defaultPath),
      m_importNotebook(false), m_manualPath(false), m_manualName(false),
      m_notebooks(p_notebooks)
{
    setupUI(title, info);

    connect(m_nameEdit, &VMetaWordLineEdit::textChanged, this, &VNewNotebookDialog::handleInputChanged);
    connect(m_pathEdit, &VLineEdit::textChanged, this, &VNewNotebookDialog::handleInputChanged);
    connect(browseBtn, &QPushButton::clicked, this, &VNewNotebookDialog::handleBrowseBtnClicked);

    handleInputChanged();
}

void VNewNotebookDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    QLabel *nameLabel = new QLabel(tr("Notebook &name:"));
    m_nameEdit = new VMetaWordLineEdit(defaultName);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    nameLabel->setBuddy(m_nameEdit);

    QLabel *pathLabel = new QLabel(tr("Notebook &root folder:"));
    m_pathEdit = new VLineEdit(defaultPath);
    pathLabel->setBuddy(m_pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));

    m_relativePathCB = new QCheckBox(tr("Use relative path"));
    m_relativePathCB->setToolTip(tr("Use relative path (to VNote's executable) in configuration file"));
    connect(m_relativePathCB, &QCheckBox::stateChanged,
            this, &VNewNotebookDialog::handleInputChanged);

    QLabel *imageFolderLabel = new QLabel(tr("&Image folder:"));
    m_imageFolderEdit = new VLineEdit();
    imageFolderLabel->setBuddy(m_imageFolderEdit);
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(g_config->getImageFolder()));
    m_imageFolderEdit->setToolTip(tr("Set the name of the folder to hold images of all the notes in this notebook "
                                     "(empty to use global configuration)"));
    imageFolderLabel->setToolTip(m_imageFolderEdit->toolTip());
    validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_imageFolderEdit);
    m_imageFolderEdit->setValidator(validator);

    QLabel *attachmentFolderLabel = new QLabel(tr("&Attachment folder:"));
    m_attachmentFolderEdit = new VLineEdit();
    attachmentFolderLabel->setBuddy(m_attachmentFolderEdit);
    m_attachmentFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                                 .arg(g_config->getAttachmentFolder()));
    m_attachmentFolderEdit->setToolTip(tr("Set the name of the folder to hold attachments of all the notes in this notebook "
                                          "(empty to use global configuration, read-only once created)"));
    attachmentFolderLabel->setToolTip(m_attachmentFolderEdit->toolTip());
    validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_attachmentFolderEdit);
    m_attachmentFolderEdit->setValidator(validator);

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(nameLabel, 0, 0);
    topLayout->addWidget(m_nameEdit, 0, 1, 1, 2);
    topLayout->addWidget(pathLabel, 1, 0);
    topLayout->addWidget(m_pathEdit, 1, 1);
    topLayout->addWidget(browseBtn, 1, 2);
    topLayout->addWidget(m_relativePathCB, 2, 1);
    topLayout->addWidget(imageFolderLabel, 3, 0);
    topLayout->addWidget(m_imageFolderEdit, 3, 1);
    topLayout->addWidget(attachmentFolderLabel, 4, 0);
    topLayout->addWidget(m_attachmentFolderEdit, 4, 1);

    // Warning label.
    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

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
    setWindowTitle(p_title);
}

QString VNewNotebookDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}

QString VNewNotebookDialog::getPathInput() const
{
    // absoluteFilePath() to convert the drive to upper case.
    // cleanPath() to remove duplicate separator, '.', and '..'.
    QString ret;
    if (isUseRelativePath()) {
        // Use relative path in config file.
        QDir appDir(QCoreApplication::applicationDirPath());
        ret = QDir::cleanPath(appDir.relativeFilePath(m_pathEdit->text()));
    } else {
        ret = QDir::cleanPath(QFileInfo(m_pathEdit->text()).absoluteFilePath());
    }

    return ret;
}

QString VNewNotebookDialog::getImageFolder() const
{
    if (m_imageFolderEdit->isEnabled()) {
        return m_imageFolderEdit->text();
    } else {
        return QString();
    }
}

QString VNewNotebookDialog::getAttachmentFolder() const
{
    if (m_attachmentFolderEdit->isEnabled()) {
        return m_attachmentFolderEdit->text();
    } else {
        return QString();
    }
}

void VNewNotebookDialog::handleBrowseBtnClicked()
{
    static QString defaultPath;

    if (defaultPath.isEmpty()) {
        defaultPath = g_config->getVnoteNotebookFolderPath();
        if (!QFileInfo::exists(defaultPath)) {
            defaultPath = g_config->getDocumentPathOrHomePath();
        }
    }

    QString dirPath = QFileDialog::getExistingDirectory(this,
                                                        tr("Select Root Folder Of The Notebook"),
                                                        defaultPath,
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        m_manualPath = true;
        if (m_pathEdit->text() == dirPath) {
            handleInputChanged();
        } else {
            m_pathEdit->setText(dirPath);
        }

        defaultPath = VUtils::basePathFromPath(dirPath);
    }
}

bool VNewNotebookDialog::isImportExistingNotebook() const
{
    return m_importNotebook;
}

void VNewNotebookDialog::showEvent(QShowEvent *event)
{
    m_nameEdit->setFocus();
    QDialog::showEvent(event);
}

void VNewNotebookDialog::handleInputChanged()
{
    QString warnText = tr("<span style=\"%1\">WARNING</span>: The folder chosen is NOT empty! "
                          "It is highly recommended to use an EMPTY and EXCLUSIVE folder for a new notebook.")
                          .arg(g_config->c_warningTextStyle);
    QString infoText = tr("<span style=\"%1\">INFO</span>: The folder chosen seems to be a root "
                          "folder of a notebook created by VNote before. "
                          "VNote will try to import it by reading the configuration file.")
                          .arg("font-weight:bold;");
    bool pathOk = false;
    bool configExist = false;
    bool showWarnLabel = false;

    // User has input some texts.
    if (m_pathEdit->isModified()) {
        m_manualPath = true;
    }

    if (m_nameEdit->isModified()) {
        m_manualName = true;
    }

    if (autoComplete()) {
        return;
    }

    QString path = m_pathEdit->text();
    if (!path.isEmpty()) {
        if (!QDir::isAbsolutePath(path)) {
            showWarnLabel = true;
            QString tmp = tr("<span style=\"%1\">WARNING</span>: Please specify absolute path.")
                            .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(tmp);
        } else if (QFileInfo::exists(path)) {
            QDir dir(path);
            QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden);
            if (files.isEmpty()) {
                pathOk = true;
            } else {
                // Folder is not empty.
                configExist = VConfigManager::directoryConfigExist(path);
                if (configExist) {
                    pathOk = true;
                    m_warnLabel->setText(infoText);
                } else {
                    m_warnLabel->setText(warnText);
                }

                showWarnLabel = true;
            }
        } else {
            pathOk = true;
        }
    }

    // Try to validate if this is a legal path on the OS.
    if (pathOk) {
        pathOk = VUtils::checkPathLegal(path);
        if (!pathOk) {
            showWarnLabel = true;
            QString tmp = tr("<span style=\"%1\">WARNING</span>: The path seems to be illegal. "
                             "Please choose another one.")
                            .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(tmp);
        }
    }

    if (pathOk) {
        // Check if this path has been in VNote.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (VUtils::equalPath(m_notebooks[idx]->getPath(), path)) {
                break;
            }
        }

        if (idx < m_notebooks.size()) {
            pathOk = false;
            showWarnLabel = true;
            QString existText = tr("<span style=\"%1\">WARNING</span>: The folder chosen has already been a root folder "
                                   "of existing notebook <span style=\"%2\">%3</span> in VNote.")
                                   .arg(g_config->c_warningTextStyle)
                                   .arg(g_config->c_dataTextStyle)
                                   .arg(m_notebooks[idx]->getName());
            m_warnLabel->setText(existText);
        }
    }

    if (pathOk && isUseRelativePath()) {
        if (!VUtils::inSameDrive(QCoreApplication::applicationDirPath(), path)) {
            pathOk = false;
            showWarnLabel = true;
            QString existText = tr("<span style=\"%1\">WARNING</span>: Please choose a folder in the same drive as "
                                   "<span style=\"%2\">%3</span> when relative path is enabled.")
                                   .arg(g_config->c_warningTextStyle)
                                   .arg(g_config->c_dataTextStyle)
                                   .arg(QCoreApplication::applicationDirPath());
            m_warnLabel->setText(existText);
        }
    }

    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (pathOk && nameOk) {
        // Check if the name conflicts with existing notebook name.
        // Case-insensitive.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (m_notebooks[idx]->getName().toLower() == name.toLower()) {
                break;
            }
        }

        QString warnText;
        if (idx < m_notebooks.size()) {
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
    m_importNotebook = configExist;
    m_imageFolderEdit->setEnabled(!m_importNotebook);
    m_attachmentFolderEdit->setEnabled(!m_importNotebook);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk && pathOk);
}

bool VNewNotebookDialog::autoComplete()
{
    QString nameText = m_nameEdit->getEvaluatedText();

    if (m_manualPath) {
        if (m_manualName) {
            return false;
        }

        // Set the name according to user-chosen path.
        QString pathText = m_pathEdit->text();
        if (!pathText.isEmpty()) {
            QString autoName = VUtils::directoryNameFromPath(pathText);
            if (autoName != nameText) {
                m_nameEdit->setText(autoName);
                return true;
            }
        }

        return false;
    }

    QString vnoteFolder = g_config->getVnoteNotebookFolderPath();
    QString pathText = m_pathEdit->text();
    if (!pathText.isEmpty()
        && !VUtils::equalPath(vnoteFolder, VUtils::basePathFromPath(pathText))) {
        return false;
    }

    bool ret = false;
    if (nameText.isEmpty()) {
        if (m_manualName) {
            return false;
        }

        // Get a folder name under vnoteFolder and set it as the name of the notebook.
        QString name = "vnotebook";
        name = VUtils::getDirNameWithSequence(vnoteFolder, name);
        m_nameEdit->setText(name);
        ret = true;
    } else {
        // Use the name as the folder name under vnoteFolder.
        QString autoPath = QDir::cleanPath(QDir(vnoteFolder).filePath(nameText));
        if (autoPath != pathText) {
            m_pathEdit->setText(autoPath);
            ret = true;
        }
    }

    return ret;
}

bool VNewNotebookDialog::isUseRelativePath() const
{
    return m_relativePathCB->isChecked();
}
