#include <QtWidgets>
#include "vnewfiledialog.h"
#include "vconfigmanager.h"
#include "vdirectory.h"
#include "vlineedit.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VNewFileDialog::VNewFileDialog(const QString &title, const QString &info,
                               const QString &defaultName, VDirectory *directory,
                               QWidget *parent)
    : QDialog(parent), title(title), info(info),
      defaultName(defaultName), m_directory(directory)
{
    setupUI();

    connect(m_nameEdit, &VLineEdit::textChanged, this, &VNewFileDialog::handleInputChanged);

    handleInputChanged();
}

void VNewFileDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }

    // Name.
    QLabel *nameLabel = new QLabel(tr("Note &name:"));
    m_nameEdit = new VLineEdit(defaultName);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    int dotIndex = defaultName.lastIndexOf('.');
    m_nameEdit->setSelection(0, (dotIndex == -1) ? defaultName.size() : dotIndex);
    nameLabel->setBuddy(m_nameEdit);

    // InsertTitle.
    m_insertTitleCB = new QCheckBox(tr("Insert note name as title (for Markdown only)"));
    m_insertTitleCB->setToolTip(tr("Insert note name into the new note as a title"));
    m_insertTitleCB->setChecked(g_config->getInsertTitleFromNoteName());
    connect(m_insertTitleCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                g_config->setInsertTitleFromNoteName(p_state == Qt::Checked);
            });

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(nameLabel, m_nameEdit);
    topLayout->addWidget(m_insertTitleCB);

    m_nameEdit->setMinimumWidth(m_insertTitleCB->sizeHint().width());

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

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

void VNewFileDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (nameOk) {
        // Check if the name conflicts with existing note name.
        // Case-insensitive when creating note.
        QString warnText;
        if (m_directory->findFile(name, false)) {
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

QString VNewFileDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}

bool VNewFileDialog::getInsertTitleInput() const
{
    return m_insertTitleCB->isChecked();
}
