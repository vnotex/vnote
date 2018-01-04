#include <QtWidgets>
#include "vnotebookinfodialog.h"
#include "vnotebook.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vmetawordlineedit.h"

extern VConfigManager *g_config;

VNotebookInfoDialog::VNotebookInfoDialog(const QString &p_title,
                                         const QString &p_info,
                                         const VNotebook *p_notebook,
                                         const QVector<VNotebook *> &p_notebooks,
                                         QWidget *p_parent)
    : QDialog(p_parent), m_notebook(p_notebook),
      m_notebooks(p_notebooks)
{
    setupUI(p_title, p_info);

    connect(m_nameEdit, &VMetaWordLineEdit::textChanged,
            this, &VNotebookInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VNotebookInfoDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
    }

    m_nameEdit = new VMetaWordLineEdit(m_notebook->getName());
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    m_nameEdit->selectAll();

    m_pathEdit = new VLineEdit(m_notebook->getPath());
    m_pathEdit->setReadOnly(true);

    // Image folder.
    m_imageFolderEdit = new VLineEdit(m_notebook->getImageFolderConfig());
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(g_config->getImageFolder()));
    m_imageFolderEdit->setToolTip(tr("Set the name of the folder to hold images of all the notes in this notebook "
                                     "(empty to use global configuration)"));
    validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), m_imageFolderEdit);
    m_imageFolderEdit->setValidator(validator);

    // Attachment folder.
    Q_ASSERT(!m_notebook->getAttachmentFolder().isEmpty());
    m_attachmentFolderEdit = new VLineEdit(m_notebook->getAttachmentFolder());
    m_attachmentFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                                 .arg(g_config->getAttachmentFolder()));
    m_attachmentFolderEdit->setToolTip(tr("The folder to hold attachments of all the notes in this notebook"));
    m_attachmentFolderEdit->setReadOnly(true);

    // Recycle bin folder.
    VLineEdit *recycleBinFolderEdit = new VLineEdit(m_notebook->getRecycleBinFolder());
    recycleBinFolderEdit->setReadOnly(true);
    recycleBinFolderEdit->setToolTip(tr("The folder to hold deleted files from within VNote of all the notes in this notebook"));

    // Created time.
    QString createdTimeStr = VUtils::displayDateTime(const_cast<VNotebook *>(m_notebook)->getCreatedTimeUtc().toLocalTime());
    QLabel *createdTimeLabel = new QLabel(createdTimeStr);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Notebook &name:"), m_nameEdit);
    topLayout->addRow(tr("Notebook &root folder:"), m_pathEdit);
    topLayout->addRow(tr("&Image folder:"), m_imageFolderEdit);
    topLayout->addRow(tr("Attachment folder:"), m_attachmentFolderEdit);
    topLayout->addRow(tr("Recycle bin folder:"), recycleBinFolderEdit);
    topLayout->addRow(tr("Created time:"), createdTimeLabel);

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

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);
}

void VNotebookInfoDialog::handleInputChanged()
{
    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    bool showWarnLabel = false;

    if (nameOk && name != m_notebook->getName()) {
        // Check if the name conflicts with existing notebook name.
        // Case-insensitive.
        int idx = -1;
        for (idx = 0; idx < m_notebooks.size(); ++idx) {
            if (m_notebooks[idx]->getName().toLower() == name.toLower()) {
                break;
            }
        }

        QString warnText;
        if (idx < m_notebooks.size() && m_notebooks[idx] != m_notebook) {
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

QString VNotebookInfoDialog::getName() const
{
    return m_nameEdit->getEvaluatedText();
}

QString VNotebookInfoDialog::getImageFolder() const
{
    return m_imageFolderEdit->text();
}

void VNotebookInfoDialog::showEvent(QShowEvent *p_event)
{
    m_nameEdit->setFocus();
    QDialog::showEvent(p_event);
}

