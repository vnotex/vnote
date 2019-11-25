#include <QtWidgets>
#include "vfileinfodialog.h"
#include "vdirectory.h"
#include "vnotefile.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vmetawordlineedit.h"

extern VConfigManager *g_config;

VFileInfoDialog::VFileInfoDialog(const QString &title,
                                 const QString &info,
                                 VDirectory *directory,
                                 const VNoteFile *file,
                                 QWidget *parent)
    : QDialog(parent), m_directory(directory), m_file(file)
{
    setupUI(title, info);

    connect(m_nameEdit, &VMetaWordLineEdit::textChanged, this, &VFileInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VFileInfoDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
    }

    // File name.
    QString name = m_file->getName();
    m_nameEdit = new VMetaWordLineEdit(name);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    int baseStart = 0, baseLength = name.size();
    int dotIdx = name.lastIndexOf('.');
    if (dotIdx != -1) {
        baseLength = dotIdx;
    }

    // Select without suffix.
    m_nameEdit->setSelection(baseStart, baseLength);

    // File path.
    QLineEdit *filePathEdit = new QLineEdit(m_file->fetchPath());
    filePathEdit->setReadOnly(true);

    // Attachment folder.
    VLineEdit *attachmentFolderEdit = new VLineEdit(m_file->getAttachmentFolder());
    attachmentFolderEdit->setPlaceholderText(tr("Will be assigned when adding attachments"));
    attachmentFolderEdit->setToolTip(tr("The folder to hold attachments of this note"));
    attachmentFolderEdit->setReadOnly(true);

    // Created time.
    QString createdTimeStr = VUtils::displayDateTime(m_file->getCreatedTimeUtc().toLocalTime());
    QLabel *createdTimeLabel = new QLabel(createdTimeStr);

    // Modified time.
    createdTimeStr = VUtils::displayDateTime(m_file->getModifiedTimeUtc().toLocalTime());
    QLabel *modifiedTimeLabel = new QLabel(createdTimeStr);
    modifiedTimeLabel->setToolTip(tr("Last modified time within VNote"));

    // Tags.
    QLineEdit *tagEdit = new QLineEdit(m_file->getTags().join(", "));
    QString tagTip = tr("Add tags to a note at the right bottom status bar when it is opened");
    tagEdit->setPlaceholderText(tagTip);
    tagEdit->setToolTip(tr("Tags of this note separated by , (%1)").arg(tagTip));
    tagEdit->setReadOnly(true);

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Note &name:"), m_nameEdit);
    topLayout->addRow(tr("File path:"), filePathEdit);
    topLayout->addRow(tr("Attachment folder:"), attachmentFolderEdit);
    topLayout->addRow(tr("Created time:"), createdTimeLabel);
    topLayout->addRow(tr("Modified time:"), modifiedTimeLabel);
    topLayout->addRow(tr("Tags:"), tagEdit);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);
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

    setWindowTitle(p_title);
}

void VFileInfoDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (nameOk && name != m_file->getName()) {
        // Check if the name conflicts with existing note name.
        // Case-insensitive when creating note.
        const VNoteFile *file = m_directory->findFile(name, false);
        QString warnText;
        if (file && file != m_file) {
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name (case-insensitive) <span style=\"%2\">%3</span> already exists. "
                          "Please choose another name.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        } else if (m_file->getDocType() != DocType::Unknown
                   && VUtils::docTypeFromName(name) != m_file->getDocType()) {
            // Check if the name change the doc type.
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Changing type of the note is not supported. "
                          "Please use the same suffix as the old one.")
                         .arg(g_config->c_warningTextStyle);
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

QString VFileInfoDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}
